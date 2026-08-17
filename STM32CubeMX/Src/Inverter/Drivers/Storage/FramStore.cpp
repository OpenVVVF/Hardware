#include "Inverter/Drivers/Storage/FramStore.h"

#include <cstring>

namespace Inverter {
namespace FramStore {

namespace {

CY15B102Q_HandleTypeDef* s_dev = nullptr;

struct Header {
    uint32_t magic;
    uint16_t node_id;
    uint16_t version;
    uint16_t length;
    uint16_t flags;
    uint32_t crc32;
};
static_assert(sizeof(Header) == HEADER_SIZE, "unexpected header size");

/* Small table-less CRC-32 (poly 0xEDB88320), fine for <= 256 B records. */
uint32_t crc32Update(uint32_t crc, const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return crc;
}

uint32_t recordCrc(const Header& h, const uint8_t* payload) {
    uint32_t crc = 0xFFFFFFFFUL;
    crc = crc32Update(crc, reinterpret_cast<const uint8_t*>(&h.node_id), sizeof(h.node_id));
    crc = crc32Update(crc, reinterpret_cast<const uint8_t*>(&h.version), sizeof(h.version));
    crc = crc32Update(crc, reinterpret_cast<const uint8_t*>(&h.length), sizeof(h.length));
    crc = crc32Update(crc, reinterpret_cast<const uint8_t*>(&h.flags), sizeof(h.flags));
    crc = crc32Update(crc, payload, h.length);
    return crc ^ 0xFFFFFFFFUL;
}

bool validNode(uint16_t node_id) {
    return node_id >= 1U && node_id <= MAX_NODE;
}

} // namespace

void init(CY15B102Q_HandleTypeDef* dev) {
    s_dev = dev;
}

bool isReady() {
    return s_dev != nullptr;
}

bool save(uint16_t node_id, uint16_t version, const void* payload, uint16_t length) {
    if (s_dev == nullptr || !validNode(node_id)) return false;
    if (payload == nullptr || length == 0U || length > MAX_PAYLOAD) return false;

    uint8_t slot[SLOT_SIZE];
    std::memset(slot, 0, sizeof(slot));

    Header h;
    h.magic = SLOT_MAGIC;
    h.node_id = node_id;
    h.version = version;
    h.length = length;
    h.flags = 0U;
    h.crc32 = recordCrc(h, static_cast<const uint8_t*>(payload));

    std::memcpy(slot, &h, sizeof(h));
    std::memcpy(slot + sizeof(h), payload, length);

    CY15B102Q_Write(s_dev, slotAddress(node_id), slot, sizeof(slot));
    return true;
}

bool load(uint16_t node_id, void* payload, uint16_t length, uint16_t* version_out) {
    if (s_dev == nullptr || !validNode(node_id)) return false;
    if (payload == nullptr || length == 0U || length > MAX_PAYLOAD) return false;

    /* SPI reads can be corrupted by power-stage switching noise (RX overrun
     * while the 10 kHz ADC ISR preempts the polling transfer).  The record
     * is validated end-to-end (magic + node id + length + CRC), so a bad
     * read is detected reliably; retry a few times with a gap so each
     * attempt lands outside any noise burst. */
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (attempt > 0) HAL_Delay(1);
        Header h;
        CY15B102Q_Read(s_dev, slotAddress(node_id), reinterpret_cast<uint8_t*>(&h), sizeof(h));

        if (h.magic != SLOT_MAGIC) continue;
        if (h.node_id != node_id) continue;
        if (h.length > MAX_PAYLOAD) continue;

        uint8_t buf[MAX_PAYLOAD];
        CY15B102Q_Read(s_dev, slotAddress(node_id) + HEADER_SIZE, buf, h.length);

        if (recordCrc(h, buf) != h.crc32) continue;

        /* Older schema versions may store a shorter payload: copy what is
         * there and zero-fill the rest so new fields read as "unset". */
        const uint16_t copy_len = (h.length < length) ? h.length : length;
        std::memcpy(payload, buf, copy_len);
        if (copy_len < length) {
            std::memset(static_cast<uint8_t*>(payload) + copy_len, 0, length - copy_len);
        }
        if (version_out != nullptr) *version_out = h.version;
        return true;
    }
    return false;
}

bool erase(uint16_t node_id) {
    if (s_dev == nullptr || !validNode(node_id)) return false;
    uint8_t slot[HEADER_SIZE];
    std::memset(slot, 0, sizeof(slot));
    CY15B102Q_Write(s_dev, slotAddress(node_id), slot, sizeof(slot));
    return true;
}

uint32_t debugCrc(uint16_t node_id, uint16_t version, uint16_t length,
                  uint16_t flags, const void* payload) {
    Header h;
    h.magic = SLOT_MAGIC;
    h.node_id = node_id;
    h.version = version;
    h.length = length;
    h.flags = flags;
    h.crc32 = 0;
    return recordCrc(h, static_cast<const uint8_t*>(payload));
}

} // namespace FramStore
} // namespace Inverter
