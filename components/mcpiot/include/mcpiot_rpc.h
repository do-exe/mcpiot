#pragma once

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCPIOT_MAX_RPC_METHODS  32

/**
 * Handler function signature.
 * @param params  cJSON object of the "params" field (may be NULL)
 * @param id      request id
 * @return heap-allocated JSON string — caller must free()
 */
typedef char *(*mcpiot_handler_t)(cJSON *params, int id);

void  mcpiot_rpc_init(void);
void  mcpiot_rpc_register(const char *method, mcpiot_handler_t handler);

/**
 * Parse a JSON-RPC 2.0 request string and dispatch to the registered handler.
 * @return heap-allocated response JSON string — caller must free()
 */
char *mcpiot_rpc_process(const char *json_str);

#ifdef __cplusplus
}
#endif
