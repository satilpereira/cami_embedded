#include "myo_armband.h"

#include <cstdio>

#include "esp_log.h"

#include "myo_bluetooth.h"

namespace {

constexpr char kTag[] = "myo_armband";

// All of Myo's 128-bit UUIDs follow d506<short>-a904-deb9-4748-2c7f4a124842
// (see myohw_services in myo_bluetooth.h). Built once from the short code
// here instead of re-typing the full UUID string at every call site.
NimBLEUUID myo_uuid(myohw_services short_uuid) {
    char buf[37];
    snprintf(buf, sizeof(buf), "d506%04x-a904-deb9-4748-2c7f4a124842", static_cast<unsigned>(short_uuid));
    return NimBLEUUID(buf);
}

} // namespace

void Armband::ClientCallbacks::onConnect(NimBLEClient* /*client*/) {
    ESP_LOGI(kTag, "Connected");
}

void Armband::ClientCallbacks::onDisconnect(NimBLEClient* /*client*/, int reason) {
    ESP_LOGW(kTag, "Disconnected, reason=%d", reason);
    owner_.connected_ = false;
}

bool Armband::connect(uint32_t scan_window_ms) {
    if (connected_) {
        return true;
    }

    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init("");
    }

    // Lazily built on first call, after NimBLEDevice::init() above.
    static const NimBLEUUID kControlServiceUuid = myo_uuid(ControlService);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);

    NimBLEAddress address;
    bool found = false;

    ESP_LOGI(kTag, "Scanning for Myo armband...");
    while (!found) {
        NimBLEScanResults results = scan->getResults(scan_window_ms, false);
        for (int i = 0; i < results.getCount(); ++i) {
            const NimBLEAdvertisedDevice* device = results.getDevice(i);
            if (device->isAdvertisingService(kControlServiceUuid)) {
                address = device->getAddress();
                found = true;
                break;
            }
        }
    }
    ESP_LOGI(kTag, "Found Myo at %s", address.toString().c_str());

    if (client_ == nullptr) {
        client_ = NimBLEDevice::createClient();
        client_->setClientCallbacks(&callbacks_, false);
    }

    if (!client_->connect(address)) {
        ESP_LOGE(kTag, "Failed to connect to Myo armband");
        return false;
    }

    connected_ = true;
    return true;
}

NimBLERemoteCharacteristic* Armband::characteristic(const NimBLEUUID& service, const NimBLEUUID& chr) {
    if (client_ == nullptr || !connected_) {
        return nullptr;
    }
    NimBLERemoteService* svc = client_->getService(service);
    if (svc == nullptr) {
        return nullptr;
    }
    return svc->getCharacteristic(chr);
}

bool Armband::subscribeCharacteristic(const NimBLEUUID& service, const NimBLEUUID& chr_uuid, bool notifications,
                                       NimBLERemoteCharacteristic::notify_callback callback) {
    NimBLERemoteCharacteristic* chr = characteristic(service, chr_uuid);
    if (chr == nullptr) {
        ESP_LOGW(kTag, "Characteristic not found for subscribe");
        return false;
    }
    if (notifications ? !chr->canNotify() : !chr->canIndicate()) {
        ESP_LOGW(kTag, "Characteristic doesn't support %s", notifications ? "notify" : "indicate");
        return false;
    }
    return chr->subscribe(notifications, callback);
}

bool Armband::readInfo(myohw_fw_info_t& out) {
    NimBLERemoteCharacteristic* chr = characteristic(myo_uuid(ControlService), myo_uuid(MyoInfoCharacteristic));
    if (chr == nullptr || !chr->canRead()) {
        ESP_LOGW(kTag, "MyoInfo characteristic not readable");
        return false;
    }
    out = chr->readValue<myohw_fw_info_t>();
    return true;
}

bool Armband::readFirmwareVersion(myohw_fw_version_t& out) {
    NimBLERemoteCharacteristic* chr =
        characteristic(myo_uuid(ControlService), myo_uuid(FirmwareVersionCharacteristic));
    if (chr == nullptr || !chr->canRead()) {
        ESP_LOGW(kTag, "FirmwareVersion characteristic not readable");
        return false;
    }
    out = chr->readValue<myohw_fw_version_t>();
    return true;
}

bool Armband::writeCommand(const void* data, size_t len) {
    NimBLERemoteCharacteristic* chr = characteristic(myo_uuid(ControlService), myo_uuid(CommandCharacteristic));
    if (chr == nullptr || !chr->canWrite()) {
        ESP_LOGW(kTag, "Command characteristic not writable");
        return false;
    }
    return chr->writeValue(static_cast<const uint8_t*>(data), len, false);
}

bool Armband::unlock(myohw_unlock_type_t type) {
    myohw_command_unlock_t cmd{};
    cmd.header.command = myohw_command_unlock;
    cmd.header.payload_size = sizeof(cmd) - sizeof(cmd.header);
    cmd.type = type;
    return writeCommand(&cmd, sizeof(cmd));
}

bool Armband::setMode(myohw_emg_mode_t emg_mode, myohw_imu_mode_t imu_mode,
                       myohw_classifier_mode_t classifier_mode) {
    myohw_command_set_mode_t cmd{};
    cmd.header.command = myohw_command_set_mode;
    cmd.header.payload_size = sizeof(cmd) - sizeof(cmd.header);
    cmd.emg_mode = emg_mode;
    cmd.imu_mode = imu_mode;
    cmd.classifier_mode = classifier_mode;
    return writeCommand(&cmd, sizeof(cmd));
}

bool Armband::vibrate(myohw_vibration_type_t type) {
    myohw_command_vibrate_t cmd{};
    cmd.header.command = myohw_command_vibrate;
    cmd.header.payload_size = sizeof(cmd) - sizeof(cmd.header);
    cmd.type = type;
    return writeCommand(&cmd, sizeof(cmd));
}

bool Armband::subscribeBattery(NimBLERemoteCharacteristic::notify_callback callback) {
    // Standard Bluetooth Battery Service -- NimBLEUUID(uint16_t) expands
    // this against the standard base UUID, not Myo's custom one.
    return subscribeCharacteristic(NimBLEUUID(static_cast<uint16_t>(BatteryService)),
                                    NimBLEUUID(static_cast<uint16_t>(BatteryLevelCharacteristic)),
                                    /*notifications=*/true, callback);
}
