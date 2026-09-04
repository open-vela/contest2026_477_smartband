#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/* Register the pinned ai_agent outbound tap for channel "mooncat". */
int openvela_ui_agent_bridge_start(void);

/* Unregister the tap and release queued notification state. */
void openvela_ui_agent_bridge_stop(void);

#ifdef __cplusplus
}
#endif
