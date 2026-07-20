#pragma once

#include "cy15b102q_driver.h"

#include <cstdint>

namespace Inverter {

/**
 * @brief Tiny generic record store on top of the CY15B102Q F-RAM.
 *
 * Data is kept in fixed-size slots, one per "node" (a subsystem that wants
 * persistence: motor config today, others later).  Each slot holds a small
 * self-describing record:
 *
 *   magic   u32  ('FNOD')
 *   node_id u16  (redundant with the slot address; catches misplacement)
 *   version u16  (payload schema version, chosen by the node)
 *   length  u16  (payload bytes in use, <= FRAM_STORE_MAX_PAYLOAD)
 *   flags   u16  (reserved, 0)
 *   crc32   u32  (over node_id, version, length, flags and payload)
 *   payload u8[FRAM_STORE_MAX_PAYLOAD]
 *
 * Slots are addressed by node id: slot address = node_id * FRAM_STORE_SLOT_SIZE.
 * Node ids 1..FRAM_STORE_MAX_NODE are valid; id 0 is reserved (the on-time
 * logger lives at address 0).  There is no directory: a slot is either a
 * valid record for that node id or treated as empty.  F-RAM needs no erase
 * and has effectively unlimited endurance, so fixed slots beat a fancy
 * log-structured layout for this use case.
 */
namespace FramStore {

constexpr uint32_t SLOT_MAGIC       = 0x464E4F44UL; /* 'FNOD' */
constexpr uint32_t SLOT_SIZE        = 256UL;
constexpr uint32_t HEADER_SIZE      = 16UL;
constexpr uint32_t MAX_PAYLOAD      = SLOT_SIZE - HEADER_SIZE;
constexpr uint16_t MAX_NODE         = 1023U; /* 256 KB / 256 B slots */

/** Node 1: motor configuration (see MotorConfigStore). */
constexpr uint16_t NODE_MOTOR_CONFIG = 1U;

/**
 * @brief Bind the store to an initialised F-RAM device.  Call once at boot
 *        before any load/save.
 */
void init(CY15B102Q_HandleTypeDef* dev);

bool isReady();

/** Slot byte address for a node id (node_id * SLOT_SIZE). */
constexpr uint32_t slotAddress(uint16_t node_id) {
    return static_cast<uint32_t>(node_id) * SLOT_SIZE;
}

/**
 * @brief Persist a payload as the record for node_id.
 * @return false if the store is not initialised, the id is invalid, or the
 *         payload exceeds MAX_PAYLOAD.
 */
bool save(uint16_t node_id, uint16_t version, const void* payload, uint16_t length);

/**
 * @brief Load the payload of the record for node_id.
 *
 * Succeeds only if the slot holds a well-formed record with a matching node
 * id and CRC, and its stored length is at least `length` bytes (so a newer
 * firmware's larger payload can still be read by an older schema that uses
 * a prefix of it).
 *
 * @param version_out  Optional: receives the stored schema version.
 * @return true on success (payload filled), false if slot empty/corrupt.
 */
bool load(uint16_t node_id, void* payload, uint16_t length, uint16_t* version_out = nullptr);

/**
 * @brief Invalidate the record for node_id (writes a zeroed slot).
 */
bool erase(uint16_t node_id);

/** Debug helper: compute the record CRC over explicit header fields +
 *  `length` payload bytes (for the motorcfg raw diagnostic). */
uint32_t debugCrc(uint16_t node_id, uint16_t version, uint16_t length,
                  uint16_t flags, const void* payload);

} // namespace FramStore
} // namespace Inverter
