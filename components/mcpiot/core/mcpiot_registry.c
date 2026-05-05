#include "mcpiot_registry.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static mcpiot_registry_t s_registry;

void mcpiot_registry_init(const char *board, const char *version)
{
    memset(&s_registry, 0, sizeof(s_registry));
    strncpy(s_registry.board,   board,   sizeof(s_registry.board)   - 1);
    strncpy(s_registry.version, version, sizeof(s_registry.version) - 1);
}

void mcpiot_registry_add_module(const char *type, int pin,
                                  const char **methods, int method_count)
{
    if (s_registry.module_count >= MCPIOT_MAX_MODULES) return;

    mcpiot_module_t *mod = &s_registry.modules[s_registry.module_count++];
    strncpy(mod->type, type, sizeof(mod->type) - 1);
    mod->pin          = pin;
    mod->method_count = (method_count < MCPIOT_MAX_METHODS_PER_MODULE)
                            ? method_count : MCPIOT_MAX_METHODS_PER_MODULE;

    for (int i = 0; i < mod->method_count; i++) {
        strncpy(mod->methods[i], methods[i], sizeof(mod->methods[i]) - 1);
    }
}

const mcpiot_registry_t *mcpiot_registry_get(void)
{
    return &s_registry;
}

char *mcpiot_registry_to_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "board",    s_registry.board);
    cJSON_AddStringToObject(root, "version",  s_registry.version);
    cJSON_AddStringToObject(root, "protocol", "mcpiot/1.0");

    cJSON *modules = cJSON_CreateArray();
    for (int i = 0; i < s_registry.module_count; i++) {
        const mcpiot_module_t *mod = &s_registry.modules[i];
        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "type", mod->type);
        if (mod->pin >= 0) {
            cJSON_AddNumberToObject(m, "pin", mod->pin);
        }
        cJSON *method_arr = cJSON_CreateArray();
        for (int j = 0; j < mod->method_count; j++) {
            cJSON_AddItemToArray(method_arr, cJSON_CreateString(mod->methods[j]));
        }
        cJSON_AddItemToObject(m, "methods", method_arr);
        cJSON_AddItemToArray(modules, m);
    }
    cJSON_AddItemToObject(root, "modules", modules);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}
