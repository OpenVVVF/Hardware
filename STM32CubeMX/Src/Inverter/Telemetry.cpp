#include "Inverter/Telemetry.h"

#include "main.h"
#include "usart.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cstdarg>

#if TELEMETRY_HAS_MEASUREMENT_SYSTEM
#include "Sensors/MeasurementSystem.h"
#endif

namespace Telemetry {

// ============================================================
// Wire protocol
// ============================================================
#pragma pack(push, 1)
struct TelemetryHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  msg_type;
    uint16_t payload_len;
    uint32_t seq;
    uint32_t time_us;
};
#pragma pack(pop)

static_assert(sizeof(TelemetryHeader) == 16, "TelemetryHeader must be 16 bytes");

enum ValueType : uint8_t {
    VT_F32 = 1,
    VT_STR = 2
};

// ============================================================
// Tunables
// ============================================================
static constexpr uint16_t MAX_DYNAMIC_KEYS      = 128;
static constexpr uint16_t MAX_SENSOR_BINDINGS   = 256;

static constexpr uint16_t LOG_QUEUE_CAP         = 512;
static constexpr uint16_t DEFINE_QUEUE_CAP      = 512;

static constexpr uint8_t  KEY_MAXLEN            = 32;
static constexpr uint8_t  STR_MAXLEN            = 48;

static constexpr uint32_t DEFAULT_PERIOD_US     = 10000;  // 100 Hz
static constexpr uint32_t DEFINE_REANNOUNCE_US  = 100000; // 10 Hz

static constexpr size_t   DEFINE_PAYLOAD_MAX    = 240;
static constexpr size_t   DATA_PAYLOAD_MAX      = 600;

static constexpr uint16_t DYNAMIC_ID_BASE       = 0x8000;

// TX ring buffer size for UART DMA transport
static constexpr size_t   TX_BUF_SIZE           = 8192;

// ============================================================
// Helpers
// ============================================================
static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static size_t cobs_encode(const uint8_t* in, size_t len, uint8_t* out, size_t out_cap) {
    if (out_cap == 0) return 0;

    size_t read_i = 0;
    size_t write_i = 1;
    size_t code_i = 0;
    uint8_t code = 1;

    while (read_i < len) {
        if (in[read_i] == 0) {
            if (write_i >= out_cap) return 0;
            out[code_i] = code;
            code = 1;
            code_i = write_i++;
            ++read_i;
            continue;
        }

        if (write_i >= out_cap) return 0;
        out[write_i++] = in[read_i++];
        if (++code == 0xFF) {
            if (write_i >= out_cap) return 0;
            out[code_i] = code;
            code = 1;
            code_i = write_i++;
        }
    }

    if (code_i >= out_cap) return 0;
    out[code_i] = code;
    return write_i;
}

static inline void put_u16(uint8_t*& w, uint16_t v) {
    *w++ = (uint8_t)(v & 0xFF);
    *w++ = (uint8_t)((v >> 8) & 0xFF);
}

static inline void put_f32(uint8_t*& w, float f) {
    static_assert(sizeof(float) == 4, "float must be 32-bit");
    std::memcpy(w, &f, 4);
    w += 4;
}

// ============================================================
// Ring queue
// ============================================================
template <typename T, uint16_t CAP>
struct RingQueue {
    T        buf[CAP]{};
    uint16_t head = 0;
    uint16_t tail = 0;

    inline bool empty() const { return head == tail; }
    inline bool full()  const { return (uint16_t)(tail + 1) % CAP == head; }

    inline bool push(const T& v) {
        if (full()) return false;
        buf[tail] = v;
        tail = (uint16_t)(tail + 1) % CAP;
        return true;
    }

    inline bool pop(T& out) {
        if (empty()) return false;
        out = buf[head];
        head = (uint16_t)(head + 1) % CAP;
        return true;
    }

    inline void reset() { head = tail = 0; }
};

// ============================================================
// UART DMA transport
// ============================================================
static UART_HandleTypeDef* g_uart = &huart3;

/* Place in RAM_D1 (AXI SRAM) so DMA1/DMA2 can access it. DTCMRAM is CPU-only. */
static uint8_t g_tx_buf[TX_BUF_SIZE] __attribute__((section(".dma_buffers")));
static volatile size_t g_tx_head = 0;
static volatile size_t g_tx_tail = 0;
static volatile size_t g_tx_dma_len = 0;
static volatile bool g_tx_dma_busy = false;

static inline bool in_isr_context() {
    return (__get_IPSR() != 0U);
}

static inline uint32_t crit_enter() {
    if (in_isr_context()) {
        return 0;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void crit_exit(uint32_t state) {
    if (!in_isr_context()) {
        __set_PRIMASK(state);
    }
}

static size_t tx_ring_count_unsafe() {
    if (g_tx_head >= g_tx_tail) {
        return g_tx_head - g_tx_tail;
    }
    return TX_BUF_SIZE - g_tx_tail + g_tx_head;
}

static void start_tx_dma_if_idle_unsafe() {
    if (g_uart == nullptr) return;
    if (g_tx_dma_busy) return;
    if (g_tx_head == g_tx_tail) return;

    size_t contiguous = (g_tx_head > g_tx_tail) ? (g_tx_head - g_tx_tail) : (TX_BUF_SIZE - g_tx_tail);
    if (contiguous == 0) return;

    g_tx_dma_len = contiguous;
    g_tx_dma_busy = true;

    if (HAL_UART_Transmit_DMA(g_uart, &g_tx_buf[g_tx_tail], (uint16_t)contiguous) != HAL_OK) {
        g_tx_dma_busy = false;
        g_tx_dma_len = 0;
    }
}

static bool uart_write_bytes(const uint8_t* data, size_t len) {
    if (g_uart == nullptr || data == nullptr || len == 0) return false;

    uint32_t irq_state = crit_enter();

    size_t free = TX_BUF_SIZE - 1 - tx_ring_count_unsafe();
    if (len > free) {
        crit_exit(irq_state);
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        g_tx_buf[g_tx_head] = data[i];
        g_tx_head = (g_tx_head + 1) % TX_BUF_SIZE;
    }

    start_tx_dma_if_idle_unsafe();

    crit_exit(irq_state);
    return true;
}

// ============================================================
// Measurement bindings
// ============================================================
#if TELEMETRY_HAS_MEASUREMENT_SYSTEM
struct SensorBinding {
    uint16_t id = 0;
    uint8_t  name_len = 0;
    char     name[KEY_MAXLEN]{};
    const MeasurementChannel* ch = nullptr;
};

static SensorBinding g_sensors[MAX_SENSOR_BINDINGS];
static uint16_t      g_sensor_count = 0;
static const MeasurementSystem* g_ms = nullptr;
#endif

// ============================================================
// Dynamic keys + queues
// ============================================================
struct DynamicKey {
    uint16_t id = 0;
    uint32_t hash = 0;
    uint8_t  type = 0;
    uint8_t  key_len = 0;
    char     key[KEY_MAXLEN]{};
    bool     used = false;

    bool     has_last_f32 = false;
    float    last_f32 = 0.0f;
};

struct DefineItem {
    uint16_t id;
    uint8_t  type;
    uint8_t  key_len;
    char     key[KEY_MAXLEN];
};

struct LogItem {
    uint16_t id = 0;
    uint8_t  type = 0;
    union {
        float f32;
        struct { uint8_t len; char bytes[STR_MAXLEN]; } str;
    } v;
};

static DynamicKey g_dyn[MAX_DYNAMIC_KEYS];
static uint16_t   g_next_dyn_id = DYNAMIC_ID_BASE;

static RingQueue<DefineItem, DEFINE_QUEUE_CAP> g_define_q;
static RingQueue<LogItem,    LOG_QUEUE_CAP>    g_log_q;
static RingQueue<LogItem,    LOG_QUEUE_CAP>    g_str_q;

// ============================================================
// Runtime state
// ============================================================
static uint32_t g_send_period_us = DEFAULT_PERIOD_US;
static uint16_t g_sensor_chunk_limit = 0;
static uint32_t g_last_send_us   = 0;
static uint32_t g_last_define_us = 0;
static uint32_t g_frame_seq      = 0;

// ============================================================
// Time source
// ============================================================
static bool g_dwt_time_ok = false;

static void init_dwt_timebase() {
#if (__CORTEX_M == 7U)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0;
    g_dwt_time_ok = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U);
#else
    g_dwt_time_ok = false;
#endif
}

static uint32_t time_us_32_internal() {
    return HAL_GetTick() * 1000U;
}

// ============================================================
// Key helpers
// ============================================================
static uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static int find_dyn_key(const char* key, uint32_t hash) {
    for (int i = 0; i < (int)MAX_DYNAMIC_KEYS; ++i) {
        if (!g_dyn[i].used) continue;
        if (g_dyn[i].hash != hash) continue;
        if (std::strncmp(g_dyn[i].key, key, KEY_MAXLEN) == 0) return i;
    }
    return -1;
}

static int alloc_dyn_key(const char* key, uint32_t hash, uint8_t type) {
    for (int i = 0; i < (int)MAX_DYNAMIC_KEYS; ++i) {
        if (g_dyn[i].used) continue;

        DynamicKey& k = g_dyn[i];
        k.used = true;
        k.id   = g_next_dyn_id++;
        k.hash = hash;
        k.type = type;

        const size_t len = std::min<size_t>(std::strlen(key), KEY_MAXLEN - 1);
        k.key_len = (uint8_t)len;
        std::memcpy(k.key, key, len);
        k.key[len] = '\0';
        return i;
    }
    return -1;
}

// ============================================================
// Definition helpers
// ============================================================
static inline void enqueue_define(uint16_t id, uint8_t type, const char* key, uint8_t key_len) {
    DefineItem d{};
    d.id = id;
    d.type = type;
    d.key_len = key_len;
    std::memset(d.key, 0, sizeof(d.key));
    if (key_len) std::memcpy(d.key, key, key_len);
    (void)g_define_q.push(d);
}

static void reserve_print_key() {
    const char* key = "print";
    const uint32_t h = fnv1a(key);

    int idx = find_dyn_key(key, h);
    if (idx >= 0) return;

    idx = alloc_dyn_key(key, h, VT_STR);
    if (idx < 0) return;

    enqueue_define(g_dyn[idx].id, g_dyn[idx].type, g_dyn[idx].key, g_dyn[idx].key_len);
}

static void enqueue_all_definitions() {
#if TELEMETRY_HAS_MEASUREMENT_SYSTEM
    for (uint16_t i = 0; i < g_sensor_count; ++i) {
        enqueue_define(g_sensors[i].id, VT_F32, g_sensors[i].name, g_sensors[i].name_len);
    }
#endif

    for (int i = 0; i < (int)MAX_DYNAMIC_KEYS; ++i) {
        if (!g_dyn[i].used) continue;
        enqueue_define(g_dyn[i].id, g_dyn[i].type, g_dyn[i].key, g_dyn[i].key_len);
    }
}

// ============================================================
// Payload builders
// ============================================================
static size_t build_define_payload(uint8_t* payload, size_t cap) {
    if (cap < 1) return 0;

    uint8_t* w = payload;
    *w++ = 0;
    uint8_t n_defs = 0;

    while (!g_define_q.empty()) {
        DefineItem d{};
        if (!g_define_q.pop(d)) break;

        const size_t need = 2 + 1 + 1 + d.key_len;
        if ((size_t)(w - payload) + need > cap) break;

        put_u16(w, d.id);
        *w++ = d.type;
        *w++ = d.key_len;
        if (d.key_len) {
            std::memcpy(w, d.key, d.key_len);
            w += d.key_len;
        }
        ++n_defs;
    }

    payload[0] = n_defs;
    return (size_t)(w - payload);
}

static void drain_log_queue_to_cache() {
    LogItem it{};
    while (g_log_q.pop(it)) {
        if (it.type == VT_F32) {
            for (int i = 0; i < (int)MAX_DYNAMIC_KEYS; ++i) {
                if (!g_dyn[i].used) continue;
                if (g_dyn[i].id != it.id) continue;
                g_dyn[i].last_f32 = it.v.f32;
                g_dyn[i].has_last_f32 = true;
                break;
            }
        } else if (it.type == VT_STR) {
            (void)g_str_q.push(it);
        }
    }
}

static size_t build_data_payload(uint8_t* payload, size_t cap) {
    if (cap < 1) return 0;

    uint8_t* w = payload;
    *w++ = 0;
    uint8_t n_items = 0;

    // Sticky dynamic floats first
    for (int i = 0; i < (int)MAX_DYNAMIC_KEYS; ++i) {
        if (!g_dyn[i].used) continue;
        if (g_dyn[i].type != VT_F32) continue;
        if (!g_dyn[i].has_last_f32) continue;

        const size_t need = 2 + 1 + 4;
        if ((size_t)(w - payload) + need > cap) break;

        put_u16(w, g_dyn[i].id);
        *w++ = VT_F32;
        put_f32(w, g_dyn[i].last_f32);
        ++n_items;
    }

#if TELEMETRY_HAS_MEASUREMENT_SYSTEM
    uint16_t sensor_added = 0;
    for (uint16_t i = 0; i < g_sensor_count; ++i) {
        if (g_sensor_chunk_limit != 0 && sensor_added >= g_sensor_chunk_limit) break;

        const auto& s = g_sensors[i];
        if (!s.ch) continue;

        const size_t need = 2 + 1 + 4;
        if ((size_t)(w - payload) + need > cap) break;

        put_u16(w, s.id);
        *w++ = VT_F32;
        put_f32(w, s.ch->getValue());
        ++n_items;
        ++sensor_added;
    }
#endif

    // Event strings last
    while (!g_str_q.empty()) {
        LogItem it{};
        if (!g_str_q.pop(it)) break;

        const size_t need = 2 + 1 + 1 + it.v.str.len;
        if ((size_t)(w - payload) + need > cap) break;

        put_u16(w, it.id);
        *w++ = VT_STR;
        *w++ = it.v.str.len;
        if (it.v.str.len) {
            std::memcpy(w, it.v.str.bytes, it.v.str.len);
            w += it.v.str.len;
        }
        ++n_items;
    }

    payload[0] = n_items;
    return (size_t)(w - payload);
}

// ============================================================
// Frame sender
// ============================================================
static bool send_frame(MsgType type, const uint8_t* payload, size_t payload_len, uint32_t now_us) {
    TelemetryHeader h{};
    h.magic = MAGIC;
    h.version = VERSION;
    h.msg_type = (uint8_t)type;
    h.payload_len = (uint16_t)payload_len;
    h.seq = g_frame_seq++;
    h.time_us = now_us;

    uint8_t raw[sizeof(TelemetryHeader) + DATA_PAYLOAD_MAX + 2];
    size_t raw_len = 0;

    std::memcpy(raw + raw_len, &h, sizeof(h));
    raw_len += sizeof(h);

    if (payload_len) {
        std::memcpy(raw + raw_len, payload, payload_len);
        raw_len += payload_len;
    }

    const uint16_t crc = crc16_ccitt(raw, raw_len);
    raw[raw_len++] = (uint8_t)(crc & 0xFF);
    raw[raw_len++] = (uint8_t)((crc >> 8) & 0xFF);

    constexpr size_t RAW_MAX  = sizeof(raw);
    constexpr size_t COBS_MAX = RAW_MAX + (RAW_MAX / 254) + 2;
    uint8_t encoded[COBS_MAX + 1];

    const size_t enc_len = cobs_encode(raw, raw_len, encoded, sizeof(encoded) - 1);
    if (!enc_len) return false;

    encoded[enc_len] = 0;
    return uart_write_bytes(encoded, enc_len + 1);
}

static bool flush_defines_now(uint32_t now_us) {
    bool wrote = false;
    while (!g_define_q.empty()) {
        uint8_t payload[DEFINE_PAYLOAD_MAX];
        const size_t len = build_define_payload(payload, sizeof(payload));
        if (len == 0) break;

        if (!send_frame(MSG_DEFINE, payload, len, now_us)) {
            break;
        }
        wrote = true;
    }
    return wrote;
}

// ============================================================
// Internal logging
// ============================================================
static bool log_core0(const char* key, float value) {
    if (!key) return false;

    const uint32_t h = fnv1a(key);
    int idx = find_dyn_key(key, h);

    if (idx < 0) {
        idx = alloc_dyn_key(key, h, VT_F32);
        if (idx < 0) return false;
        enqueue_define(g_dyn[idx].id, g_dyn[idx].type, g_dyn[idx].key, g_dyn[idx].key_len);
    } else if (g_dyn[idx].type != VT_F32) {
        return false;
    }

    g_dyn[idx].last_f32 = value;
    g_dyn[idx].has_last_f32 = true;
    return true;
}

static bool log_core0(const char* key, const char* value) {
    if (!key || !value) return false;

    const uint32_t h = fnv1a(key);
    int idx = find_dyn_key(key, h);

    if (idx < 0) {
        idx = alloc_dyn_key(key, h, VT_STR);
        if (idx < 0) return false;
        enqueue_define(g_dyn[idx].id, g_dyn[idx].type, g_dyn[idx].key, g_dyn[idx].key_len);
    } else if (g_dyn[idx].type != VT_STR) {
        return false;
    }

    LogItem it{};
    it.id = g_dyn[idx].id;
    it.type = VT_STR;

    const size_t len = std::min<size_t>(std::strlen(value), STR_MAXLEN);
    it.v.str.len = (uint8_t)len;
    std::memcpy(it.v.str.bytes, value, len);
    if (len < STR_MAXLEN) it.v.str.bytes[len] = '\0';

    return g_log_q.push(it);
}

// ============================================================
// Public API
// ============================================================
void set_period_us(uint32_t period_us) {
    g_send_period_us = period_us;
}

uint32_t get_period_us() {
    return g_send_period_us;
}

void set_sensor_chunk_limit(uint16_t max_sensors_per_frame) {
    g_sensor_chunk_limit = max_sensors_per_frame;
}

uint16_t get_sensor_chunk_limit() {
    return g_sensor_chunk_limit;
}

void init() {
    init(&huart3);
}

void init(UART_HandleTypeDef* uart) {
    g_uart = uart;

#if TELEMETRY_HAS_MEASUREMENT_SYSTEM
    g_ms = nullptr;
    g_sensor_count = 0;
#endif

    g_define_q.reset();
    g_log_q.reset();
    g_str_q.reset();

    for (auto& k : g_dyn) k = DynamicKey{};
    g_next_dyn_id = DYNAMIC_ID_BASE;

    g_frame_seq = 0;
    g_last_send_us = 0;
    g_last_define_us = 0;

    g_tx_head = 0;
    g_tx_tail = 0;
    g_tx_dma_len = 0;
    g_tx_dma_busy = false;

    init_dwt_timebase();
    reserve_print_key();
}

bool log(const char* key, float value) {
    return log_core0(key, value);
}

bool log(const char* key, const char* value) {
    return log_core0(key, value);
}

bool printf(const char* fmt, ...) {
    if (!fmt) return false;

    char buf[STR_MAXLEN + 1];

    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0) return false;
    buf[STR_MAXLEN] = '\0';

    return Telemetry::log("print", buf);
}

#if TELEMETRY_HAS_MEASUREMENT_SYSTEM
void bindMeasurementSystem(const MeasurementSystem& ms) {
    g_ms = &ms;
    g_sensor_count = 0;

    const auto& sensors = ms.sensors();
    const size_t n = std::min<size_t>(sensors.size(), MAX_SENSOR_BINDINGS);

    for (size_t i = 0; i < n; ++i) {
        const auto& s = sensors[i];
        if (s.id >= DYNAMIC_ID_BASE) continue;

        auto& b = g_sensors[g_sensor_count++];
        b.id = s.id;
        b.ch = s.ch;

        const size_t name_len = std::min<size_t>(s.name.size(), KEY_MAXLEN - 1);
        b.name_len = (uint8_t)name_len;
        std::memset(b.name, 0, sizeof(b.name));
        if (name_len) std::memcpy(b.name, s.name.data(), name_len);
        b.name[name_len] = '\0';

        enqueue_define(b.id, VT_F32, b.name, b.name_len);
    }
}

bool updateSensors(const MeasurementSystem& ms) {
    if (g_ms != &ms) bindMeasurementSystem(ms);
    return updateSensors();
}
#endif

bool updateSensors() {
#if TELEMETRY_HAS_MEASUREMENT_SYSTEM
    if (!g_ms && g_sensor_count == 0 && g_define_q.empty() && g_log_q.empty()) {
        return false;
    }
#endif

    const uint32_t now = time_us_32_internal();

    if ((uint32_t)(now - g_last_define_us) > DEFINE_REANNOUNCE_US) {
        enqueue_all_definitions();
        g_last_define_us = now;
    }

    if ((uint32_t)(now - g_last_send_us) < g_send_period_us) {
        return false;
    }
    g_last_send_us = now;

    bool wrote = false;

    drain_log_queue_to_cache();

    wrote |= flush_defines_now(now);

    {
        uint8_t payload[DATA_PAYLOAD_MAX];
        const size_t len = build_data_payload(payload, sizeof(payload));
        if (len > 1) {
            wrote |= send_frame(MSG_DATA, payload, len, now);
        }
    }

    return wrote;
}

void onUartTxComplete(UART_HandleTypeDef* huart) {
    if (huart == nullptr || g_uart == nullptr) return;
    if (huart->Instance != g_uart->Instance) return;

    uint32_t irq_state = crit_enter();

    g_tx_tail = (g_tx_tail + g_tx_dma_len) % TX_BUF_SIZE;
    g_tx_dma_len = 0;
    g_tx_dma_busy = false;
    start_tx_dma_if_idle_unsafe();

    crit_exit(irq_state);
}

bool txBusy() {
    return g_tx_dma_busy;
}

size_t txBytesQueued() {
    uint32_t irq_state = crit_enter();
    size_t n = tx_ring_count_unsafe();
    crit_exit(irq_state);
    return n;
}

} // namespace Telemetry

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
    Telemetry::onUartTxComplete(huart);
}
