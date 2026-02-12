#include "Telemetry.h"
#include "pico/stdlib.h"
#include <cstring>
#include <cstdint>
#include <algorithm>

// Forward decls from your project
#include "Sensors/MeasurementSystem.h"
#include "Command/CommandContext.h"

namespace Telemetry {

// ---------------- CRC16-CCITT (0x1021, init 0xFFFF) ----------------
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

// ---------------- COBS encode ----------------
static size_t cobs_encode(const uint8_t* in, size_t len, uint8_t* out, size_t out_cap) {
    size_t read_index = 0;
    size_t write_index = 1;
    size_t code_index = 0;
    uint8_t code = 1;
    if (out_cap == 0) return 0;

    while (read_index < len) {
        if (in[read_index] == 0) {
            if (write_index >= out_cap) return 0;
            out[code_index] = code;
            code = 1;
            code_index = write_index++;
            read_index++;
        } else {
            if (write_index >= out_cap) return 0;
            out[write_index++] = in[read_index++];
            code++;
            if (code == 0xFF) {
                if (write_index >= out_cap) return 0;
                out[code_index] = code;
                code = 1;
                code_index = write_index++;
            }
        }
    }

    if (code_index >= out_cap) return 0;
    out[code_index] = code;
    return write_index;
}

static inline void write_bytes_stdio(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) putchar_raw((char)data[i]);
}

// ---------------- Protocol structs ----------------
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

static_assert(sizeof(TelemetryHeader) == 16, "Header size mismatch");

// ---------------- Types ----------------
enum ValueType : uint8_t {
    VT_F32 = 1,
    VT_STR = 2,
};

// ---------------- Limits ----------------
static constexpr uint16_t MAX_KEYS   = 128;  // for dynamic keys created via Telemetry::log()
static constexpr uint8_t  KEY_MAXLEN = 32;
static constexpr uint8_t  STR_MAXLEN = 48;

static constexpr uint16_t LOGQ_MAX   = 256;
static constexpr uint16_t DEFQ_MAX   = 256;  // allow initial sensor defs burst
static constexpr uint16_t MAX_SENSORS = 128; // max MeasurementSystem sensors exposed to telemetry

// ---------------- Key registry (dynamic keys only; no heap) ----------------
// DEFINE sends: [u16 id][u8 type][u8 key_len][key bytes]
// DATA sends:   repeated items: [u16 id][u8 type][value...]
struct KeyEntry {
    uint16_t id = 0;
    uint32_t hash = 0;
    uint8_t  type = 0;
    uint8_t  key_len = 0;
    char     key[KEY_MAXLEN] = {};
    bool     used = false;
};

static KeyEntry g_keys[MAX_KEYS];
static uint16_t g_next_id = 1; // will be set in init() to avoid collisions with sensor IDs

// FNV-1a 32-bit
static uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static int find_key_index(const char* key, uint32_t h) {
    for (int i = 0; i < (int)MAX_KEYS; ++i) {
        if (!g_keys[i].used) continue;
        if (g_keys[i].hash != h) continue;
        if (std::strncmp(g_keys[i].key, key, KEY_MAXLEN) == 0) return i;
    }
    return -1;
}

static int allocate_key(const char* key, uint32_t h, uint8_t type) {
    for (int i = 0; i < (int)MAX_KEYS; ++i) {
        if (g_keys[i].used) continue;

        g_keys[i].used = true;
        g_keys[i].id   = g_next_id++;
        g_keys[i].hash = h;
        g_keys[i].type = type;

        const size_t klen = std::min<size_t>(std::strlen(key), KEY_MAXLEN - 1);
        g_keys[i].key_len = (uint8_t)klen;
        std::memcpy(g_keys[i].key, key, klen);
        g_keys[i].key[klen] = '\0';
        return i;
    }
    return -1;
}

// ---------------- Log queue (no heap) ----------------
struct LogItem {
    uint16_t id = 0;
    uint8_t  type = 0;
    union {
        float f32;
        struct {
            uint8_t len;
            char    bytes[STR_MAXLEN];
        } str;
    } v;
};

static LogItem g_logq[LOGQ_MAX];
static uint16_t g_q_head = 0;
static uint16_t g_q_tail = 0;

static inline bool q_empty() { return g_q_head == g_q_tail; }
static inline bool q_full()  { return (uint16_t)(g_q_tail + 1) % LOGQ_MAX == g_q_head; }

static bool q_push(const LogItem& it) {
    if (q_full()) return false;
    g_logq[g_q_tail] = it;
    g_q_tail = (uint16_t)(g_q_tail + 1) % LOGQ_MAX;
    return true;
}

static bool q_pop(LogItem& out) {
    if (q_empty()) return false;
    out = g_logq[g_q_head];
    g_q_head = (uint16_t)(g_q_head + 1) % LOGQ_MAX;
    return true;
}

// ---------------- Define queue ----------------
struct DefItem {
    uint16_t id;
    uint8_t  type;
    uint8_t  key_len;
    char     key[KEY_MAXLEN];
};

static DefItem g_defq[DEFQ_MAX];
static uint16_t g_d_head = 0;
static uint16_t g_d_tail = 0;

static inline bool d_empty() { return g_d_head == g_d_tail; }
static inline bool d_full()  { return (uint16_t)(g_d_tail + 1) % DEFQ_MAX == g_d_head; }

static bool d_push(uint16_t id, uint8_t type, const char* key, uint8_t key_len) {
    if (d_full()) return false;
    DefItem& d = g_defq[g_d_tail];
    d.id = id;
    d.type = type;
    d.key_len = key_len;
    std::memset(d.key, 0, sizeof(d.key));
    if (key_len) std::memcpy(d.key, key, key_len);
    g_d_tail = (uint16_t)(g_d_tail + 1) % DEFQ_MAX;
    return true;
}

static bool d_pop(DefItem& out) {
    if (d_empty()) return false;
    out = g_defq[g_d_head];
    g_d_head = (uint16_t)(g_d_head + 1) % DEFQ_MAX;
    return true;
}

// ---------------- Sensor bindings (from MeasurementSystem) ----------------
struct SensorBinding {
    uint16_t id = 0;
    uint8_t  name_len = 0;
    char     name[KEY_MAXLEN] = {};
    const MeasurementChannel* ch = nullptr;  // read-only access
};

static SensorBinding g_sensors[MAX_SENSORS];
static uint16_t g_sensor_count = 0;

// ---------------- Rate limiting ----------------
static uint32_t g_seq = 0;
static uint32_t g_period_us = 10000; // 100 Hz default
static uint32_t g_last_send_us = 0;

void set_period_us(uint32_t period_us) { g_period_us = period_us; }
uint32_t get_period_us() { return g_period_us; }

// Re-announce defines periodically (helps host resync)
static uint32_t g_last_define_us = 0;
static constexpr uint32_t DEFINE_REANNOUNCE_US = 100000; // 10 Hz (tune as desired)

// ---------------- Small helpers ----------------
static inline void put_u16(uint8_t*& w, uint16_t v) { *w++ = (uint8_t)(v & 0xFF); *w++ = (uint8_t)(v >> 8); }
static inline void put_u32(uint8_t*& w, uint32_t v) { *w++ = (uint8_t)(v); *w++ = (uint8_t)(v >> 8); *w++ = (uint8_t)(v >> 16); *w++ = (uint8_t)(v >> 24); }
static inline void put_f32(uint8_t*& w, float f) {
    static_assert(sizeof(float) == 4, "float must be 32-bit");
    std::memcpy(w, &f, 4);
    w += 4;
}

static void reset_queues_and_tables() {
    // defs + logs
    g_q_head = g_q_tail = 0;
    g_d_head = g_d_tail = 0;

    // dynamic keys
    for (auto& k : g_keys) k = KeyEntry{};
}

// Push *all* known definitions again (sensor defs + dynamic keys)
static void enqueue_all_definitions() {
    // 1) Measurement sensors
    for (uint16_t i = 0; i < g_sensor_count; ++i) {
        const auto& s = g_sensors[i];
        d_push(s.id, VT_F32, s.name, s.name_len);
    }
    // 2) Dynamic keys
    for (int i = 0; i < (int)MAX_KEYS; ++i) {
        if (!g_keys[i].used) continue;
        d_push(g_keys[i].id, g_keys[i].type, g_keys[i].key, g_keys[i].key_len);
    }
}

// ---------------- Public logging API (dynamic extra keys) ----------------
bool log(const char* key, float value) {
    if (!key) return false;
    const uint32_t h = fnv1a(key);
    int idx = find_key_index(key, h);
    if (idx < 0) {
        idx = allocate_key(key, h, VT_F32);
        if (idx < 0) return false;
        d_push(g_keys[idx].id, g_keys[idx].type, g_keys[idx].key, g_keys[idx].key_len);
    } else if (g_keys[idx].type != VT_F32) {
        return false;
    }

    LogItem it{};
    it.id = g_keys[idx].id;
    it.type = VT_F32;
    it.v.f32 = value;
    return q_push(it);
}

bool log(const char* key, const char* value) {
    if (!key || !value) return false;
    const uint32_t h = fnv1a(key);
    int idx = find_key_index(key, h);
    if (idx < 0) {
        idx = allocate_key(key, h, VT_STR);
        if (idx < 0) return false;
        d_push(g_keys[idx].id, g_keys[idx].type, g_keys[idx].key, g_keys[idx].key_len);
    } else if (g_keys[idx].type != VT_STR) {
        return false;
    }

    LogItem it{};
    it.id = g_keys[idx].id;
    it.type = VT_STR;

    const size_t len = std::min<size_t>(std::strlen(value), STR_MAXLEN);
    it.v.str.len = (uint8_t)len;
    std::memcpy(it.v.str.bytes, value, len);
    if (len < STR_MAXLEN) it.v.str.bytes[len] = '\0';
    return q_push(it);
}

// ---------------- Init from MeasurementSystem ----------------
// Call once at startup, after MeasurementSystem has added channels.
void init(const MeasurementSystem& ms) {
    reset_queues_and_tables();

    g_seq = 0;
    g_last_send_us = 0;
    g_last_define_us = 0;

    // Build sensor bindings from MeasurementSystem registry
    g_sensor_count = 0;
    const auto& sensors = ms.sensors();

    const size_t n = std::min<size_t>(sensors.size(), MAX_SENSORS);
    for (size_t i = 0; i < n; ++i) {
        const auto& s = sensors[i];

        SensorBinding& b = g_sensors[g_sensor_count++];
        b.id = s.id;
        b.ch = s.ch;

        const size_t name_len = std::min<size_t>(s.name.size(), KEY_MAXLEN - 1);
        b.name_len = (uint8_t)name_len;
        std::memset(b.name, 0, sizeof(b.name));
        if (name_len) std::memcpy(b.name, s.name.data(), name_len);
        b.name[name_len] = '\0';

        // Queue a DEFINE for this sensor immediately
        d_push(b.id, VT_F32, b.name, b.name_len);
    }

    // Ensure dynamic keys allocated via Telemetry::log() won't collide with sensor IDs.
    // Sensor IDs are 1..N in your scheme, so start after max sensor id.
    uint16_t max_id = 0;
    for (uint16_t i = 0; i < g_sensor_count; ++i) max_id = std::max<uint16_t>(max_id, g_sensors[i].id);
    g_next_id = (uint16_t)(max_id + 1);
}

// ---------------- Payload building ----------------
static size_t build_define_payload(uint8_t* payload, size_t cap) {
    // format: [u8 n_defs] repeated: [u16 id][u8 type][u8 key_len][key bytes]
    if (cap < 1) return 0;
    uint8_t* w = payload;
    *w++ = 0; // n_defs placeholder
    uint8_t n = 0;

    while (!d_empty()) {
        DefItem d{};
        if (!d_pop(d)) break;

        const size_t need = 2 + 1 + 1 + d.key_len;
        if ((size_t)(w - payload) + need > cap) break;

        put_u16(w, d.id);
        *w++ = d.type;
        *w++ = d.key_len;
        if (d.key_len) {
            std::memcpy(w, d.key, d.key_len);
            w += d.key_len;
        }
        n++;
    }

    payload[0] = n;
    return (size_t)(w - payload);
}

static size_t build_data_payload(uint8_t* payload, size_t cap) {
    // format:
    // [u8 n_items]
    // repeated items:
    //   [u16 id][u8 type][value...]
    if (cap < 1) return 0;
    uint8_t* w = payload;
    *w++ = 0; // n_items placeholder
    uint8_t n = 0;

    // 1) Sensor snapshot from MeasurementSystem (no hardcoded list)
    for (uint16_t i = 0; i < g_sensor_count; ++i) {
        const auto& s = g_sensors[i];
        if (!s.ch) continue;

        const size_t need = 2 + 1 + 4;
        if ((size_t)(w - payload) + need > cap) break;

        put_u16(w, s.id);
        *w++ = VT_F32;
        put_f32(w, s.ch->getValue());
        n++;
    }

    // 2) Drain queued dynamic log items
    while (!q_empty()) {
        LogItem it{};
        if (!q_pop(it)) break;

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
            if (it.v.str.len) {
                std::memcpy(w, it.v.str.bytes, it.v.str.len);
                w += it.v.str.len;
            }
        }
        n++;
    }

    payload[0] = n;
    return (size_t)(w - payload);
}

// ---------------- Public updateSensors (sends frames) ----------------
bool updateSensors(const MeasurementSystem& /*ms*/) {
    const uint32_t now = time_us_32();

    // Periodically re-announce defines so host can resync
    if ((uint32_t)(now - g_last_define_us) > DEFINE_REANNOUNCE_US) {
        enqueue_all_definitions();
        g_last_define_us = now;
    }

    if ((uint32_t)(now - g_last_send_us) < g_period_us) return false;
    g_last_send_us = now;

    bool wrote_any = false;

    // 1) Send DEFINE frame if queued
    if (!d_empty()) {
        uint8_t payload[240];
        const size_t p_len = build_define_payload(payload, sizeof(payload));
        if (p_len > 0) {
            TelemetryHeader h{};
            h.magic = MAGIC;
            h.version = VERSION;
            h.msg_type = MSG_DEFINE;
            h.payload_len = (uint16_t)p_len;
            h.seq = g_seq++;
            h.time_us = now;

            uint8_t raw[16 + sizeof(payload) + 2];
            size_t off = 0;
            std::memcpy(raw + off, &h, sizeof(h)); off += sizeof(h);
            std::memcpy(raw + off, payload, p_len); off += p_len;

            const uint16_t crc = crc16_ccitt(raw, off);
            raw[off++] = (uint8_t)(crc & 0xFF);
            raw[off++] = (uint8_t)((crc >> 8) & 0xFF);

            constexpr size_t RAW_MAX = sizeof(raw);
            constexpr size_t COBS_MAX = RAW_MAX + (RAW_MAX / 254) + 2;
            uint8_t enc[COBS_MAX];

            const size_t enc_len = cobs_encode(raw, off, enc, sizeof(enc));
            if (enc_len) {
                write_bytes_stdio(enc, enc_len);
                putchar_raw('\0');
                wrote_any = true;
            }
        }
    }

    // 2) Send DATA frame (sensor snapshot + queued logs)
    {
        uint8_t payload[300];
        const size_t p_len = build_data_payload(payload, sizeof(payload));
        if (p_len > 1) {
            TelemetryHeader h{};
            h.magic = MAGIC;
            h.version = VERSION;
            h.msg_type = MSG_DATA;
            h.payload_len = (uint16_t)p_len;
            h.seq = g_seq++;
            h.time_us = now;

            uint8_t raw[16 + sizeof(payload) + 2];
            size_t off = 0;
            std::memcpy(raw + off, &h, sizeof(h)); off += sizeof(h);
            std::memcpy(raw + off, payload, p_len); off += p_len;

            const uint16_t crc = crc16_ccitt(raw, off);
            raw[off++] = (uint8_t)(crc & 0xFF);
            raw[off++] = (uint8_t)((crc >> 8) & 0xFF);

            constexpr size_t RAW_MAX = sizeof(raw);
            constexpr size_t COBS_MAX = RAW_MAX + (RAW_MAX / 254) + 2;
            uint8_t enc[COBS_MAX];

            const size_t enc_len = cobs_encode(raw, off, enc, sizeof(enc));
            if (enc_len) {
                write_bytes_stdio(enc, enc_len);
                putchar_raw('\0');
                wrote_any = true;
            }
        }
    }

    return wrote_any;
}

} // namespace Telemetry
