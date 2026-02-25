/**
 * NTRIP Client (Rover Mode)
 * Connects to NTRIP caster to receive RTCM corrections
 */

#ifndef NTRIP_CLIENT_H
#define NTRIP_CLIENT_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Connect to NTRIP caster as a client (rover)
 */
esp_err_t ntrip_client_connect(void);

/**
 * Disconnect from NTRIP caster
 */
void ntrip_client_disconnect(void);

/**
 * Check if connected to NTRIP caster
 */
bool ntrip_client_is_connected(void);

/**
 * Receive RTCM data from NTRIP caster
 * Returns number of bytes received, 0 if no data, or -1 on error
 */
int ntrip_client_receive(uint8_t *buffer, size_t max_len);

/**
 * Get total bytes received
 */
uint32_t ntrip_client_get_bytes_received(void);

/**
 * Check if connection is stale (connected but no data for >15 sec)
 */
bool ntrip_client_is_stale(void);

/**
 * Force reconnect if connection is stale
 */
void ntrip_client_check_stale(void);

#endif // NTRIP_CLIENT_H
