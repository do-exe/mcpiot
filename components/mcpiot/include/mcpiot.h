#pragma once

#include "mcpiot_registry.h"
#include "mcpiot_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCPIOT_VERSION  "1.0.0"

typedef struct {
    const char               *board;    // e.g. "esp32s3"
    const char               *version;  // firmware version string
    mcpiot_transport_config_t transport;
} mcpiot_config_t;

/**
 * Initialise the registry, RPC dispatcher, and transport layer.
 * Call before mcpiot_start().
 */
void mcpiot_init(const mcpiot_config_t *config);

/**
 * Start the RPC listener task. Call after all modules are registered.
 */
void mcpiot_start(void);

#ifdef __cplusplus
}
#endif
