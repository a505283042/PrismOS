/* SPDX-License-Identifier: MIT */
#include "aster_network.h"
const aster_network_ops_t aster_network_backend={
.size=sizeof(aster_network_ops_t),.version=ASTER_NETWORK_API_VERSION,.name="firmware",
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_interface_info=aster_net_interface_info,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_open=aster_net_open,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_restart=aster_net_restart,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_finish=aster_net_finish,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_snapshot=aster_net_snapshot,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_send=aster_net_send,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_recv=aster_net_recv,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_cancel=aster_net_cancel,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_close=aster_net_close,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_close_owner=aster_net_close_owner,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.socket_get_stats=aster_net_get_stats,
#endif
#if defined(CONFIG_ASTER_HTTP) && CONFIG_ASTER_HTTP
.http_start=aster_http_svc_start,
#endif
#if defined(CONFIG_ASTER_HTTP) && CONFIG_ASTER_HTTP
.http_status=aster_http_svc_status,
#endif
#if defined(CONFIG_ASTER_HTTP) && CONFIG_ASTER_HTTP
.http_read=aster_http_svc_read,
#endif
#if defined(CONFIG_ASTER_HTTP) && CONFIG_ASTER_HTTP
.http_header=aster_http_svc_header,
#endif
#if defined(CONFIG_ASTER_HTTP) && CONFIG_ASTER_HTTP
.http_cancel=aster_http_svc_cancel,
#endif
#if defined(CONFIG_ASTER_HTTP) && CONFIG_ASTER_HTTP
.http_close=aster_http_svc_close,
#endif
#if defined(CONFIG_ASTER_HTTP) && CONFIG_ASTER_HTTP
.http_close_owner=aster_http_svc_close_owner,
#endif
#if defined(CONFIG_ASTER_HTTP) && CONFIG_ASTER_HTTP
.http_ca_set=aster_http_svc_ca_set,
#endif
#if defined(CONFIG_ASTER_HTTP) && CONFIG_ASTER_HTTP
.http_get_stats=aster_http_svc_get_stats,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_open=aster_mqtt_svc_open,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_publish=aster_mqtt_svc_publish,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_subscribe=aster_mqtt_svc_subscribe,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_status=aster_mqtt_svc_status,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_operation=aster_mqtt_svc_operation,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_recv=aster_mqtt_svc_recv,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_cancel=aster_mqtt_svc_cancel,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_close=aster_mqtt_svc_close,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_close_owner=aster_mqtt_svc_close_owner,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_stats=aster_mqtt_svc_stats,
#endif
#if defined(CONFIG_ASTER_MQTT) && CONFIG_ASTER_MQTT
.mqtt_reconnects=aster_mqtt_svc_reconnects,
#endif
#if defined(CONFIG_ASTER_WEBSOCKET) && CONFIG_ASTER_WEBSOCKET
.ws_open=aster_ws_svc_open,
#endif
#if defined(CONFIG_ASTER_WEBSOCKET) && CONFIG_ASTER_WEBSOCKET
.ws_send=aster_ws_svc_send,
#endif
#if defined(CONFIG_ASTER_WEBSOCKET) && CONFIG_ASTER_WEBSOCKET
.ws_status=aster_ws_svc_status,
#endif
#if defined(CONFIG_ASTER_WEBSOCKET) && CONFIG_ASTER_WEBSOCKET
.ws_recv=aster_ws_svc_recv,
#endif
#if defined(CONFIG_ASTER_WEBSOCKET) && CONFIG_ASTER_WEBSOCKET
.ws_close=aster_ws_svc_close,
#endif
#if defined(CONFIG_ASTER_WEBSOCKET) && CONFIG_ASTER_WEBSOCKET
.ws_cancel=aster_ws_svc_cancel,
#endif
#if defined(CONFIG_ASTER_WEBSOCKET) && CONFIG_ASTER_WEBSOCKET
.ws_close_owner=aster_ws_svc_close_owner,
#endif
#if defined(CONFIG_ASTER_WEBSOCKET) && CONFIG_ASTER_WEBSOCKET
.ws_stats=aster_ws_svc_stats,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.event_take=aster_net_event_take,
#endif
#if defined(CONFIG_ASTER_NET) && CONFIG_ASTER_NET
.event_drop_owner=aster_net_event_drop_owner,
#endif
};
