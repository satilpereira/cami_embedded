#pragma once

#include <NimBLEDevice.h>

#include "myo_bluetooth.h"

// Central-role BLE client for a single Myo armband (Thalmic Labs).
// Steps 1-3 of docs/plans/myo-library-migration.md: scan + connect,
// synchronous device info / firmware version reads, and unlock/mode/
// vibrate commands.
class Armband {
public:
    // Scans for a Myo armband (retrying the scan window until one is
    // found) and connects to it. Blocks until connected.
    bool connect(uint32_t scan_window_ms = 5000);

    bool isConnected() const { return connected_; }

    // Synchronous reads, mainly useful as a smoke test that the connection
    // and service discovery actually work before adding notification
    // subscriptions (Step 4+). Returns false if not connected or the
    // characteristic couldn't be read; `out` is left untouched in that case.
    bool readInfo(myohw_fw_info_t& out);
    bool readFirmwareVersion(myohw_fw_version_t& out);

    // Commands. The Myo ships locked/idle, so unlock() + setMode() are
    // required before any streaming happens (Step 4+). Returns false if
    // not connected or the write failed.
    bool unlock(myohw_unlock_type_t type = myohw_unlock_hold);
    bool setMode(myohw_emg_mode_t emg_mode, myohw_imu_mode_t imu_mode,
                 myohw_classifier_mode_t classifier_mode);
    bool vibrate(myohw_vibration_type_t type);

    // Battery Service is a standard Bluetooth service (not one of Myo's
    // custom ones). Subscribes to battery level notifications. Returns
    // false if not connected or the characteristic doesn't support notify.
    bool subscribeBattery(NimBLERemoteCharacteristic::notify_callback callback);

private:
    class ClientCallbacks : public NimBLEClientCallbacks {
    public:
        explicit ClientCallbacks(Armband& owner) : owner_(owner) {}
        void onConnect(NimBLEClient* client) override;
        void onDisconnect(NimBLEClient* client, int reason) override;

    private:
        Armband& owner_;
    };

    // Looks up a characteristic by (service, characteristic) UUID, or
    // nullptr if not connected / not found. Takes UUIDs directly (rather
    // than myohw_services) so it works for both Myo's custom UUID space
    // and standard Bluetooth services like Battery.
    NimBLERemoteCharacteristic* characteristic(const NimBLEUUID& service, const NimBLEUUID& chr);

    // Looks up a characteristic and subscribes to it (notify or indicate).
    // Centralized so each Step 4+ subscribe method only picks the UUIDs
    // and notify/indicate mode, not the lookup+subscribe dance.
    bool subscribeCharacteristic(const NimBLEUUID& service, const NimBLEUUID& chr, bool notifications,
                                  NimBLERemoteCharacteristic::notify_callback callback);

    // Writes a packed myohw_command_*_t struct to the command
    // characteristic. Centralized here so each command method only builds
    // its struct, not the characteristic lookup dance.
    bool writeCommand(const void* data, size_t len);

    NimBLEClient* client_ = nullptr;
    ClientCallbacks callbacks_{*this};
    bool connected_ = false;
};
