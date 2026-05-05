#include "mcpiot_event.h"
#include "mcpiot_transport.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mcpiot_event";

#define MAX_EVENTS 16

static char s_event_names[MAX_EVENTS][32];
static int s_event_count = 0;

void mcpiot_event_register(const char *event_name)
{
    if (s_event_count >= MAX_EVENTS)
        return;
    strncpy(s_event_names[s_event_count++], event_name, 31);
    ESP_LOGI(TAG, "registered: %s", event_name);
}

void mcpiot_event_fire(const char *event_name, const char *payload_json)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"jsonrpc\":\"2.0\",\"method\":\"event.notify\","
             "\"params\":{\"event\":\"%s\",\"data\":%s}}\n",
             event_name, payload_json ? payload_json : "null");
    mcpiot_transport_send(buf, strlen(buf));
    ESP_LOGI(TAG, "fired: %s", event_name);
}

void mcpiot_event_subscribe(const char *event_name, int client_id)
{
    ESP_LOGI(TAG, "subscribe: %s (client %d)", event_name, client_id);
    /* TODO: track per-client subscriptions */
}

void mcpiot_event_unsubscribe(const char *event_name, int client_id)
{
    ESP_LOGI(TAG, "unsubscribe: %s (client %d)", event_name, client_id);
}
