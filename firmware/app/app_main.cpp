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

static const char *pose_name(uint16_t pose) {
    switch (pose) {
        case myohw_pose_rest: return "rest";
        case myohw_pose_fist: return "fist";
        case myohw_pose_wave_in: return "wave in";
        case myohw_pose_wave_out: return "wave out";
        case myohw_pose_fingers_spread: return "fingers spread";
        case myohw_pose_double_tap: return "double tap";
        default: return "unknown";
    }
}

// Connects (or reconnects) to the Myo and redoes the full command +
// subscription sequence. Called once at boot and again from the main loop
// whenever a disconnect is detected (Step 9) -- mirrors the reconnect block
// in myo_mqtt_bridge.ino's loop(), which redid the same steps rather than
// assuming subscriptions/mode survive a disconnect.
static void setup_myo() {
    // Blocks until found and connected (docs/plans/myo-library-migration.md).
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

    myo.setMode(myohw_emg_mode_send_emg, myohw_imu_mode_send_data, myohw_classifier_mode_enabled);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Step 8: force the Myo to never auto-sleep, since we want it
    // streaming continuously rather than dozing off after inactivity.
    myo.setSleepMode(myohw_sleep_mode_never_sleep);
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

    // Step 5+6: EMG and IMU both work (confirmed), but they stream fast
    // enough to bury the sparse gesture events below in log noise. Flip
    // this to 1 to bring them back.
#if 0
    myo.subscribeEmg([](NimBLERemoteCharacteristic* /*chr*/, uint8_t* pData, size_t length, bool /*isNotify*/) {
        if (length < sizeof(myohw_emg_data_t)) {
            return;
        }
        const auto* emg = reinterpret_cast<const myohw_emg_data_t*>(pData);
        ESP_LOGI(TAG, "EMG %d %d %d %d %d %d %d %d | %d %d %d %d %d %d %d %d", emg->sample1[0],
                 emg->sample1[1], emg->sample1[2], emg->sample1[3], emg->sample1[4], emg->sample1[5],
                 emg->sample1[6], emg->sample1[7], emg->sample2[0], emg->sample2[1], emg->sample2[2],
                 emg->sample2[3], emg->sample2[4], emg->sample2[5], emg->sample2[6], emg->sample2[7]);
    });

    myo.subscribeImu([](NimBLERemoteCharacteristic* /*chr*/, uint8_t* pData, size_t length, bool /*isNotify*/) {
        if (length < sizeof(myohw_imu_data_t)) {
            return;
        }
        const auto* imu = reinterpret_cast<const myohw_imu_data_t*>(pData);
        ESP_LOGI(TAG, "IMU quat(%d %d %d %d) accel(%d %d %d) gyro(%d %d %d)", imu->orientation.w,
                 imu->orientation.x, imu->orientation.y, imu->orientation.z, imu->accelerometer[0],
                 imu->accelerometer[1], imu->accelerometer[2], imu->gyroscope[0], imu->gyroscope[1],
                 imu->gyroscope[2]);
    });
#endif

    // Step 7: subscribe to classifier events (poses + arm sync state).
    myo.subscribeGesture([](NimBLERemoteCharacteristic* /*chr*/, uint8_t* pData, size_t length, bool /*isNotify*/) {
        if (length < sizeof(myohw_classifier_event_t)) {
            return;
        }
        const auto* event = reinterpret_cast<const myohw_classifier_event_t*>(pData);
        switch (event->type) {
            case myohw_classifier_event_pose:
                ESP_LOGI(TAG, "Gesture: %s", pose_name(event->pose));
                break;
            case myohw_classifier_event_arm_synced:
                ESP_LOGI(TAG, "Arm synced (arm=%u x_direction=%u)", event->arm, event->x_direction);
                break;
            case myohw_classifier_event_arm_unsynced:
                ESP_LOGI(TAG, "Arm unsynced");
                break;
            case myohw_classifier_event_unlocked:
                ESP_LOGI(TAG, "Unlocked");
                break;
            case myohw_classifier_event_locked:
                ESP_LOGI(TAG, "Locked");
                break;
            case myohw_classifier_event_sync_failed:
                ESP_LOGI(TAG, "Sync failed (result=%u)", event->sync_result);
                break;
            default:
                ESP_LOGI(TAG, "Classifier event type=%u", event->type);
                break;
        }
    });
}

extern "C" void app_main(void) {
    hal_init();
    ESP_LOGI(TAG, "cami_embedded application started");

    // Configure the GPIO pin
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    setup_myo();

    while (1) {
        // Step 9: reconnect robustness. onDisconnect (myo_armband.cpp)
        // flips isConnected() to false; re-running setup_myo() here
        // rescans/reconnects and redoes unlock/mode/sleep/subscriptions,
        // since none of that survives a BLE disconnect.
        if (!myo.isConnected()) {
            ESP_LOGW(TAG, "Myo disconnected, reconnecting...");
            setup_myo();
        }

        // Turn LED ON
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Safer, modern macro for delays

        // Turn LED OFF
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
