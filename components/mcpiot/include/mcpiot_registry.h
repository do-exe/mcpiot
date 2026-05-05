#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCPIOT_MAX_MODULES             16
#define MCPIOT_MAX_METHODS_PER_MODULE  8
#define MCPIOT_MAX_STR_LEN             32

typedef struct {
    char type[MCPIOT_MAX_STR_LEN];         // e.g. "pir_sensor", "buzzer", "gpio"
    int  pin;                               // -1 if not pin-specific
    char methods[MCPIOT_MAX_METHODS_PER_MODULE][MCPIOT_MAX_STR_LEN];
    int  method_count;
} mcpiot_module_t;

typedef struct {
    char board[MCPIOT_MAX_STR_LEN];        // e.g. "esp32s3"
    char version[16];                       // firmware version
    mcpiot_module_t modules[MCPIOT_MAX_MODULES];
    int  module_count;
} mcpiot_registry_t;

void                     mcpiot_registry_init(const char *board, const char *version);
void                     mcpiot_registry_add_module(const char *type, int pin,
                                                     const char **methods, int method_count);
const mcpiot_registry_t *mcpiot_registry_get(void);
char                    *mcpiot_registry_to_json(void);  // caller must free()

#ifdef __cplusplus
}
#endif
