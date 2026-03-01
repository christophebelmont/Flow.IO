/**
 * @file BluetoothDisable.cpp
 * @brief Force-disable Bluetooth stack usage in Arduino core.
 */

extern "C" bool btInUse()
{
    // Arduino core checks this during initArduino(); returning false
    // releases BTDM controller memory for Wi-Fi-only firmware.
    return false;
}

