#include "Telemetry.h"
#include "pico/stdlib.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>

#include "Sensors/MeasurementSystem.h" // MeasurementSystem + MeasurementChannel

namespace Telemetry {

// ============================================================
// Wire protocol (unchanged)
// ============================================================
#pragma pack(push, 1)
struct TelemetryHeader {
    uint32_t magic;       // MAGIC
    uint8_t  version;     // VERSION
    uint8_t  msg_type;    // MsgType
    uint16_t payload_len; // bytes
    uint32_t seq;         // increments
    uint32_t time_us;     // time_us_32()
};
#pragma pack(pop)

static_assert(sizeof(TelemetryHeader) == 16, "TelemetryHeader must be 16 bytes");

enum ValueType : uint8_t { VT_F32 = 1, VT_STR = 2 };

// ============================================================
// Tunables
// ============================================================
static constexpr uint16_t MAX_DYNAMIC_KEYS      = 128;   // for Telemetry::log()
static constexpr uint16_t MAX_SENSOR_BINDINGS   = 128;   // exposed MeasurementSystem sensors

static constexpr uint16_t LOG_QUEUE_CAP         = 256;
static constexpr uint16_t DEFINE_QUEUE_CAP      = 256;

static constexpr uint8_t  KEY_MAXLEN            = 32;
static constexpr uint8_t  STR_MAXLEN            = 48;

static constexpr uint32_t DEFAULT_PERIOD_US     = 1000;   // 100 Hz
static constexpr uint32_t DEFINE_REANNOUNCE_US  = 100000;  // 10 Hz (host resync aid)

static constexpr size_t   DEFINE_PAYLOAD_MAX    = 400;
static constexpr size_t   DATA_PAYLOAD_MAX      = 500;

// To decouple init order safely: dynamic key IDs live in a high range
// so they never collide with MeasurementSystem sensor IDs (typically 1..N).
static constexpr uint16_t DYNAMIC_ID_BASE       = 0x8000;

// ============================================================
// CRC16-CCITT + COBS + byte writers
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

    size_t read_i = 0, write_i = 1, code_i = 0;
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

static inline void write_stdio_bytes(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) putchar_raw((char)data[i]);
}

static inline void put_u16(uint8_t*& w, uint16_t v) { *w++ = (uint8_t)(v & 0xFF); *w++ = (uint8_t)(v >> 8); }
static inline void put_f32(uint8_t*& w, float f) {
    static_assert(sizeof(float) == 4, "float must be 32-bit");
    std::memcpy(w, &f, 4);
    w += 4;
}

// ============================================================
// Fixed-size ring queue (no heap)
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
// Sensor bindings (captured once for fast update)
// ============================================================
struct SensorBinding {
    uint16_t id = 0;
    uint8_t  name_len = 0;
    char     name[KEY_MAXLEN]{};
    const MeasurementChannel* ch = nullptr;
};

static SensorBinding g_sensors[MAX_SENSOR_BINDINGS];
static uint16_t      g_sensor_count = 0;

// ============================================================
// Dynamic keys for Telemetry::log()
// ============================================================
struct DynamicKey {
    uint16_t id = 0;
    uint32_t hash = 0;
    uint8_t  type = 0;
    uint8_t  key_len = 0;
    char     key[KEY_MAXLEN]{};
    bool     used = false;
};

static uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)(*s++); h *= 16777619u; }
    return h;
}

static DynamicKey g_dyn[MAX_DYNAMIC_KEYS];
static uint16_t   g_next_dyn_id = DYNAMIC_ID_BASE;

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
// Outgoing queues: DEFINE + DATA logs
// ============================================================
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

static RingQueue<DefineItem, DEFINE_QUEUE_CAP> g_define_q;
static RingQueue<LogItem,    LOG_QUEUE_CAP>   g_log_q;

// ============================================================
// Runtime state
// ============================================================
static const MeasurementSystem* g_ms = nullptr;

static uint32_t g_send_period_us = DEFAULT_PERIOD_US;
static uint32_t g_last_send_us   = 0;
static uint32_t g_last_define_us = 0;
static uint32_t g_frame_seq      = 0;

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
    (void)g_define_q.push(d); // drop if full
}

static void enqueue_all_definitions() {
    for (uint16_t i = 0; i < g_sensor_count; ++i) {
        enqueue_define(g_sensors[i].id, VT_F32, g_sensors[i].name, g_sensors[i].name_len);
    }
    for (int i = 0; i < (int)MAX_DYNAMIC_KEYS; ++i) {
        if (!g_dyn[i].used) continue;
        enqueue_define(g_dyn[i].id, g_dyn[i].type, g_dyn[i].key, g_dyn[i].key_len);
    }
}

// ============================================================
// Payload builders (unchanged format)
// ============================================================
static size_t build_define_payload(uint8_t* payload, size_t cap) {
    if (cap < 1) return 0;

    uint8_t* w = payload;
    *w++ = 0; // n_defs placeholder
    uint8_t n_defs = 0;

    while (!g_define_q.empty()) {
        DefineItem d{};
        if (!g_define_q.pop(d)) break;

        const size_t need = 2 + 1 + 1 + d.key_len;
        if ((size_t)(w - payload) + need > cap) break;

        put_u16(w, d.id);
        *w++ = d.type;
        *w++ = d.key_len;
        if (d.key_len) { std::memcpy(w, d.key, d.key_len); w += d.key_len; }
        ++n_defs;
    }

    payload[0] = n_defs;
    return (size_t)(w - payload);
}

static size_t build_data_payload(uint8_t* payload, size_t cap) {
    if (cap < 1) return 0;

    uint8_t* w = payload;
    *w++ = 0; // n_items placeholder
    uint8_t n_items = 0;

    // 1) Sensor snapshot (all bound sensors)
    for (uint16_t i = 0; i < g_sensor_count; ++i) {
        const auto& s = g_sensors[i];
        if (!s.ch) continue;

        const size_t need = 2 + 1 + 4;
        if ((size_t)(w - payload) + need > cap) break;

        put_u16(w, s.id);
        *w++ = VT_F32;
        put_f32(w, s.ch->getValue());
        ++n_items;
    }

    // 2) Drain queued dynamic logs
    while (!g_log_q.empty()) {
        LogItem it{};
        if (!g_log_q.pop(it)) break;

        size_t need = 0;
        if (it.type == VT_F32) need = 2 + 1 + 4;
        else if (it.type == VT_STR) need = 2 + 1 + 1 + it.v.str.len;
        else continue;

        if ((size_t)(w - payload) + need > cap) break;

        put_u16(w, it.id);
        *w++ = it.type;

        if (it.type == VT_F32) {
            put_f32(w, it.v.f32);
        } else {
            *w++ = it.v.str.len;
            if (it.v.str.len) { std::memcpy(w, it.v.str.bytes, it.v.str.len); w += it.v.str.len; }
        }
        ++n_items;
    }

    payload[0] = n_items;
    return (size_t)(w - payload);
}

// ============================================================
// Frame sender (unchanged framing)
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

    std::memcpy(raw + raw_len, &h, sizeof(h)); raw_len += sizeof(h);
    if (payload_len) { std::memcpy(raw + raw_len, payload, payload_len); raw_len += payload_len; }

    const uint16_t crc = crc16_ccitt(raw, raw_len);
    raw[raw_len++] = (uint8_t)(crc & 0xFF);
    raw[raw_len++] = (uint8_t)((crc >> 8) & 0xFF);

    constexpr size_t RAW_MAX  = sizeof(raw);
    constexpr size_t COBS_MAX = RAW_MAX + (RAW_MAX / 254) + 2;
    uint8_t encoded[COBS_MAX];

    const size_t enc_len = cobs_encode(raw, raw_len, encoded, sizeof(encoded));
    if (!enc_len) return false;

    write_stdio_bytes(encoded, enc_len);
    putchar_raw('\0');
    return true;
}

// ============================================================
// Public API
// ============================================================
void set_period_us(uint32_t period_us) { g_send_period_us = period_us; }
uint32_t get_period_us() { return g_send_period_us; }

void init() {
    g_ms = nullptr;
    g_sensor_count = 0;

    g_define_q.reset();
    g_log_q.reset();

    for (auto& k : g_dyn) k = DynamicKey{};
    g_next_dyn_id = DYNAMIC_ID_BASE;

    g_frame_seq = 0;
    g_last_send_us = 0;
    g_last_define_us = 0;
}

void bindMeasurementSystem(const MeasurementSystem& ms) {
    g_ms = &ms;

    // Snapshot sensor list into fixed bindings (fast update loop)
    g_sensor_count = 0;
    const auto& sensors = ms.sensors();
    const size_t n = std::min<size_t>(sensors.size(), MAX_SENSOR_BINDINGS);

    for (size_t i = 0; i < n; ++i) {
        const auto& s = sensors[i];

        // Protect against collision with dynamic id range
        if (s.id >= DYNAMIC_ID_BASE) continue;

        SensorBinding& b = g_sensors[g_sensor_count++];
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

bool log(const char* key, float value) {
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

    LogItem it{};
    it.id = g_dyn[idx].id;
    it.type = VT_F32;
    it.v.f32 = value;
    return g_log_q.push(it);
}

bool log(const char* key, const char* value) {
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

bool updateSensors() {
    if (!g_ms) return false;

    const uint32_t now = time_us_32();

    if ((uint32_t)(now - g_last_define_us) > DEFINE_REANNOUNCE_US) {
        enqueue_all_definitions();
        g_last_define_us = now;
    }

    if ((uint32_t)(now - g_last_send_us) < g_send_period_us) return false;
    g_last_send_us = now;

    bool wrote = false;

    if (!g_define_q.empty()) {
        uint8_t payload[DEFINE_PAYLOAD_MAX];
        const size_t len = build_define_payload(payload, sizeof(payload));
        if (len > 0) wrote |= send_frame(MSG_DEFINE, payload, len, now);
    }

    {
        uint8_t payload[DATA_PAYLOAD_MAX];
        const size_t len = build_data_payload(payload, sizeof(payload));
        if (len > 1) wrote |= send_frame(MSG_DATA, payload, len, now);
    }

    return wrote;
}

bool updateSensors(const MeasurementSystem& ms) {
    if (g_ms != &ms) bindMeasurementSystem(ms);
    return updateSensors();
}

} // namespace Telemetry
