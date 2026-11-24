#include <zephyr/sys/util.h>
/* SPDX-License-Identifier: MIT */
#include "aster_sysinfo.h"
#include "aster_platform.h"
#include "aster_player_service.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

void aster_sysinfo_get(aster_system_info_t *info)
{
	memset(info, 0, sizeof(*info));
	info->size = sizeof(*info);
	info->version = 1;
	info->abi_version = ASTER_ABI_VERSION;

	strncpy(info->platform, CONFIG_ASTER_PLATFORM, sizeof(info->platform) - 1);
	strncpy(info->board, CONFIG_ASTER_BOARD_ID, sizeof(info->board) - 1);
	strncpy(info->soc, CONFIG_ASTER_SOC, sizeof(info->soc) - 1);

	info->cpu_arch = CONFIG_ASTER_CPU_ARCH_ID;
	info->cpu_freq_hz = CONFIG_ASTER_CPU_FREQ_HZ;
	info->cpu_count = CONFIG_ASTER_CPU_COUNT;

	/* Sizes come from the board profile (Kconfig), not hard-coded here. */
	info->ram_internal = CONFIG_ASTER_RAM_INTERNAL;
	info->ram_external = CONFIG_ASTER_RAM_EXTERNAL;
	info->flash_size = CONFIG_ASTER_FLASH_SIZE;

	uint64_t f = 0;
	/* Capability bits are declared by the board profile via ASTER_HAS_* so
	 * this file never references a specific driver Kconfig symbol. */
	if (IS_ENABLED(CONFIG_ASTER_HAS_DISPLAY) && aster_plat_display_count() > 0) {
		f |= 1ull << ASTER_FEATURE_DISPLAY;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_FRAMEBUFFER) && aster_plat_display_count() > 0) {
		f |= 1ull << ASTER_FEATURE_FRAMEBUFFER;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_FILESYSTEM)) {
		f |= 1ull << ASTER_FEATURE_FILESYSTEM;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_PSRAM)) {
		f |= 1ull << ASTER_FEATURE_PSRAM;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_RTC)) {
		f |= 1ull << ASTER_FEATURE_RTC;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_TOUCH) && aster_plat_input_touch_available()) {
		f |= 1ull << ASTER_FEATURE_TOUCH;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_KEYS)) {
		f |= 1ull << ASTER_FEATURE_KEYBOARD;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_ACCEL)) {
		f |= 1ull << ASTER_FEATURE_ACCELEROMETER;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_GYRO)) {
		f |= 1ull << ASTER_FEATURE_GYROSCOPE;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_CAMERA)) {
		f |= 1ull << ASTER_FEATURE_CAMERA;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_AUDIO_IN)) {
		f |= 1ull << ASTER_FEATURE_AUDIO_INPUT;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_AUDIO_OUT)) {
		f |= 1ull << ASTER_FEATURE_AUDIO_OUTPUT;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_WIFI)) {
		f |= 1ull << ASTER_FEATURE_WIFI;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_MOUSE)) {
		f |= 1ull << ASTER_FEATURE_MOUSE;
	}
	if (IS_ENABLED(CONFIG_ASTER_HAS_BT)) {
		f |= 1ull << ASTER_FEATURE_BLUETOOTH;
	}
	if (IS_ENABLED(CONFIG_ASTER_NET))
		f |= 1ull << ASTER_FEATURE_NETWORK;
	if (IS_ENABLED(CONFIG_ASTER_HTTP))
		f |= 1ull << ASTER_FEATURE_HTTP;
	if (IS_ENABLED(CONFIG_ASTER_MQTT))
		f |= 1ull << ASTER_FEATURE_MQTT;
	if (IS_ENABLED(CONFIG_ASTER_WEBSOCKET))
		f |= 1ull << ASTER_FEATURE_WEBSOCKET;
	if (IS_ENABLED(CONFIG_ASTER_HTTP_TLS))
		f |= 1ull << ASTER_FEATURE_TLS;
	if (IS_ENABLED(CONFIG_ASTER_AUDIO)) {
		f |= (1ull << ASTER_FEATURE_AUDIO_STREAM) | (1ull << ASTER_FEATURE_AUDIO_EFFECTS) |
		     (1ull << ASTER_FEATURE_AUDIO_EFFECTS_MEMORY);
	}
	if (aster_player_service_available())
		f |= 1ull << ASTER_FEATURE_MEDIA_PLAYER;
	info->feature_flags = f;
}

bool aster_sysinfo_has_feature(uint32_t bit)
{
	aster_system_info_t info;
	aster_sysinfo_get(&info);
	if (bit > 63) {
		return false;
	}
	return (info.feature_flags & (1ull << bit)) != 0;
}
