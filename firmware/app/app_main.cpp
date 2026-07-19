#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"

// Wrap C-based ESP-IDF and FreeRTOS headers inside extern "C"
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
}

#include "hal.h" // Keep this outside if hal.h is already written in C++
#include "myo_armband.h"

#define BLINK_GPIO GPIO_NUM_2 // Using the proper explicit enum type

static const char *TAG = "app";
static Armband myo;

extern "C" void app_main(void) {
    hal_init();
    ESP_LOGI(TAG, "cami_embedded application started");

    // Configure the GPIO pin
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    // Step 1 of the Myo migration: block here until the armband is found
    // and connected (docs/plans/myo-library-migration.md). The blink loop
    // below then doubles as a crude "connected" heartbeat.
    myo.connect();

    // Step 2: smoke-test the connection with a couple of synchronous reads.
    myohw_fw_info_t info{};
    if (myo.readInfo(info)) {
        ESP_LOGI(TAG,
                 "Myo serial %02x:%02x:%02x:%02x:%02x:%02x unlock_pose=%u sku=%u",
                 info.serial_number[0], info.serial_number[1], info.serial_number[2],
                 info.serial_number[3], info.serial_number[4], info.serial_number[5],
                 info.unlock_pose, info.sku);
    }

    myohw_fw_version_t fw{};
    if (myo.readFirmwareVersion(fw)) {
        ESP_LOGI(TAG, "Myo firmware %u.%u.%u (hw rev %u)", fw.major, fw.minor, fw.patch,
                 fw.hardware_rev);
    }

    // Step 3: unlock + set mode. The Myo needs a beat to process each
    // command before the next one lands -- delays match the proven
    // myo_mqtt_bridge.ino sequence.
    myo.unlock(myohw_unlock_hold);
    vTaskDelay(pdMS_TO_TICKS(100));

    myo.setMode(myohw_emg_mode_send_emg, myohw_imu_mode_none, myohw_classifier_mode_disabled);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Physical confirmation that the command channel actually works.
    myo.vibrate(myohw_vibration_short);

    // Step 4: subscribe to battery level notifications (standard Bluetooth
    // Battery Service, not Myo-specific).
    myo.subscribeBattery([](NimBLERemoteCharacteristic* /*chr*/, uint8_t* pData, size_t length, bool /*isNotify*/) {
        if (length >= 1) {
            ESP_LOGI(TAG, "Battery: %u%%", pData[0]);
        }
    });

    while (1) {
        // Turn LED ON
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Safer, modern macro for delays

        // Turn LED OFF
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
