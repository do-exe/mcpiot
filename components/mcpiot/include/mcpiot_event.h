#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Register an event name this device can emit.
     * Call from a module's register_events() callback.
     */
    void mcpiot_event_register(const char *event_name);

    /**
     * Fire an event — sends a JSON-RPC 2.0 notification to all subscribers.
     * @param event_name   e.g. "motion.detected"
     * @param payload_json JSON string for the "data" field, or NULL
     */
    void mcpiot_event_fire(const char *event_name, const char *payload_json);

    /* ── Subscription management (called by RPC dispatcher) ───────────────── */
    void mcpiot_event_subscribe(const char *event_name, int client_id);
    void mcpiot_event_unsubscribe(const char *event_name, int client_id);

#ifdef __cplusplus
}
#endif
