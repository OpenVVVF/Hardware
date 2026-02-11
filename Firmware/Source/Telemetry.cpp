#include "Telemetry.h"
#include "pico/stdlib.h"
#include <cstring>

// Forward decls from your project
#include "Sensors/MeasurementSystem.h"
#include "Command/CommandContext.h"
#include "RtBridge.h" // for RtStatus if you want it

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
// Encodes input into out. Returns encoded length (no delimiter).
// You must append a 0x00 delimiter yourself.
static size_t cobs_encode(const uint8_t* in, size_t len, uint8_t* out, size_t out_cap) {
    // Worst case expansion is + len/254 + 1
    // Caller should provide enough out_cap.
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

// ---------------- Packet structs ----------------
#pragma pack(push, 1)
struct TelemetryHeader {
    uint32_t magic;       // MAGIC
    uint8_t  version;     // VERSION
    uint8_t  msg_type;    // MSG_TELEMETRY
    uint16_t payload_len; // bytes
    uint32_t seq;         // increments
    uint32_t time_us;     // time_us_32()
};

struct TelemetryPayloadV1 {
    float v_dc;
    float v_u;
    float v_v;
    float v_w;

    float i_dc_main;
    float i_u;
    float i_w;

    float enc_sin;
    float enc_cos;
    float rotor_deg;

    float sensor_rate_khz;
};
#pragma pack(pop)

static_assert(sizeof(TelemetryHeader) == 16, "Header size mismatch");
static_assert(sizeof(TelemetryPayloadV1) == 44, "Payload size mismatch");

static uint32_t g_seq = 0;
static uint32_t g_period_us = 10000; // 100 Hz default
static uint32_t g_last_send_us = 0;

void set_period_us(uint32_t period_us) { g_period_us = period_us; }
uint32_t get_period_us() { return g_period_us; }

static inline void write_bytes_stdio(const uint8_t* data, size_t n) {
    // Very fast raw writes; stdio over USB CDC.
    // putchar_raw is in pico/stdlib.h
    for (size_t i = 0; i < n; ++i) putchar_raw((char)data[i]);
}

bool send_frame(const MeasurementSystem& ms,
                const CommandContext& ctx,
                float sensor_rate_khz)
{
    const uint32_t now = time_us_32();
    if ((uint32_t)(now - g_last_send_us) < g_period_us) return false;
    g_last_send_us = now;

    TelemetryPayloadV1 p{};
    p.v_dc = ms.read("V_DC_BUS");
    p.v_u  = ms.read("V_PH_U");
    p.v_v  = ms.read("V_PH_V");
    p.v_w  = ms.read("V_PH_W");

    p.i_dc_main = ms.read("I_DC_MAIN");
    p.i_u       = ms.read("I_PH_U");
    p.i_w       = ms.read("I_PH_W");

    p.enc_sin   = ms.read("ENCODER_SIN");
    p.enc_cos   = ms.read("ENCODER_COS");
    p.rotor_deg = ms.getRotorPositionDegrees();

    p.sensor_rate_khz = sensor_rate_khz;

    TelemetryHeader h{};
    h.magic = MAGIC;
    h.version = VERSION;
    h.msg_type = MSG_TELEMETRY;
    h.payload_len = (uint16_t)sizeof(TelemetryPayloadV1);
    h.seq = g_seq++;
    h.time_us = now;

    // Build raw buffer: header + payload + crc
    uint8_t raw[16 + 44 + 2];
    size_t off = 0;
    std::memcpy(raw + off, &h, sizeof(h)); off += sizeof(h);
    std::memcpy(raw + off, &p, sizeof(p)); off += sizeof(p);

    const uint16_t crc = crc16_ccitt(raw, off);
    raw[off++] = (uint8_t)(crc & 0xFF);
    raw[off++] = (uint8_t)((crc >> 8) & 0xFF);

    // COBS encode
    // Worst-case expansion: n + n/254 + 1
    constexpr size_t RAW_MAX = sizeof(raw);
    constexpr size_t COBS_MAX = RAW_MAX + (RAW_MAX / 254) + 2;
    uint8_t enc[COBS_MAX];

    const size_t enc_len = cobs_encode(raw, off, enc, sizeof(enc));
    if (enc_len == 0) return false;

    // Write encoded + delimiter 0x00
    write_bytes_stdio(enc, enc_len);
    putchar_raw('\0');
    return true;
}

} // namespace Telemetry
