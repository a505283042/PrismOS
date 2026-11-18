# SPDX-License-Identifier: MIT
# Shared source inventory. Build-system and OS decisions belong to callers.
set(ASTER_COMMON_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(ASTER_COMMON_INCLUDE_DIRS
    "${ASTER_COMMON_ROOT}/port"
    "${ASTER_COMMON_ROOT}/include"
    "${ASTER_COMMON_ROOT}/package"
    "${ASTER_COMMON_ROOT}/net"
    "${ASTER_COMMON_ROOT}/setup"
    "${ASTER_COMMON_ROOT}/audio" "${ASTER_COMMON_ROOT}/ui")
set(ASTER_COMMON_DEVICE_SOURCES "${ASTER_COMMON_ROOT}/port/aster_device.c" "${ASTER_COMMON_ROOT}/port/aster_power.c" "${ASTER_COMMON_ROOT}/port/aster_power_device.c" "${ASTER_COMMON_ROOT}/port/aster_power_session.c")
set(ASTER_COMMON_OS_SOURCES "${ASTER_COMMON_ROOT}/port/aster_os.c")
set(ASTER_COMMON_SERVICE_SOURCES "${ASTER_COMMON_ROOT}/port/aster_services.c")
set(ASTER_COMMON_PORT_CHECK "${ASTER_COMMON_ROOT}/port/aster_port_check.c")
set(ASTER_COMMON_DEPLOY "${ASTER_COMMON_ROOT}/port/aster_deploy.c")
set(ASTER_COMMON_NETWORK_PORT "${ASTER_COMMON_ROOT}/port/aster_network.c")
set(ASTER_COMMON_SETUP_SOURCES "${ASTER_COMMON_ROOT}/setup/aster_setup_core.c")
set(ASTER_COMMON_PACKAGE_SOURCES "${ASTER_COMMON_ROOT}/package/aster_pkg_decode.c")
set(ASTER_COMMON_HTTP_SOURCES "${ASTER_COMMON_ROOT}/net/aster_http_codec.c")
set(ASTER_COMMON_MQTT_SOURCES "${ASTER_COMMON_ROOT}/net/aster_mqtt_codec.c")
set(ASTER_COMMON_WS_SOURCES "${ASTER_COMMON_ROOT}/net/aster_ws_codec.c")

set(ASTER_COMMON_AUDIO_SOURCES "${ASTER_COMMON_ROOT}/audio/aster_audio_output.c" "${ASTER_COMMON_ROOT}/audio/aster_player_service.c")

set(ASTER_COMMON_MEDIA_SOURCES "${ASTER_COMMON_ROOT}/audio/aster_audio_decoder.c" "${ASTER_COMMON_ROOT}/audio/aster_media.c" "${ASTER_COMMON_ROOT}/audio/aster_rate_convert.c")

set(ASTER_COMMON_POWER_UI "${ASTER_COMMON_ROOT}/ui/aster_power_ui.c")
