#pragma once

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32
#define BOARD_NAME "esp32"
#elif CONFIG_IDF_TARGET_ESP32S3
#define BOARD_NAME "esp32s3"
#elif CONFIG_IDF_TARGET_ESP32C3
#define BOARD_NAME "esp32c3"
#else
#error "Unsupported board: add pin/config definitions for this target in firmware/config/board.h"
#endif

/*
 * Board-specific pin assignments and peripheral config go here, keyed off the
 * same CONFIG_IDF_TARGET_* macros, e.g.:
 *
 * #if CONFIG_IDF_TARGET_ESP32
 * #define BOARD_LED_GPIO 2
 * #elif CONFIG_IDF_TARGET_ESP32S3
 * #define BOARD_LED_GPIO 48
 * #endif
 */
