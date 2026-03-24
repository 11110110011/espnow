#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * ESP-NOW message types
 * --------------------------------------------------------------------- */
#define ESPNOW_MSG_REGISTER      0x01
#define ESPNOW_MSG_ACK           0x02
#define ESPNOW_MSG_CMD           0x03
#define ESPNOW_MSG_STATE_REPORT  0x04
#define ESPNOW_MSG_PING          0x05
#define ESPNOW_MSG_PONG          0x06

/* CMD action values */
#define ESPNOW_ACTION_OFF        0
#define ESPNOW_ACTION_ON         1
#define ESPNOW_ACTION_TOGGLE     2

/* -----------------------------------------------------------------------
 * Wire-format structs (packed, 11 bytes total)
 * --------------------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct {
    uint8_t msg_type;
    uint8_t node_id;
    uint8_t seq;
    uint8_t payload[8];
} espnow_msg_t;
#pragma pack(pop)

/* -----------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------- */

/**
 * @brief Initialise ESP-NOW in master/gateway role.
 *        Registers receive callback; starts keepalive timer.
 */
esp_err_t espnow_master_init(void);

/**
 * @brief Send ON/OFF/TOGGLE command to a node by logical ID.
 * @param action  ESPNOW_ACTION_ON / OFF / TOGGLE
 */
esp_err_t espnow_master_send_cmd(uint8_t node_id, uint8_t action);

/** @brief Send PING to all online nodes (called by keepalive timer). */
esp_err_t espnow_master_ping_all(void);

#ifdef __cplusplus
}
#endif
