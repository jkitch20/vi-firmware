#include "bluetooth.h"
#include "util/log.h"
#include "gpio.h"

#define BLUETOOTH_ENABLE_PORT 0
#define BLUETOOTH_ENABLE_PIN 17

#define BLUETOOTH_STATUS_PORT 0
#define BLUETOOTH_STATUS_PIN 18

using openxc::gpio::setGpioValue;
using openxc::gpio::setGpioDirection;
using openxc::gpio::getGpioValue;
using openxc::gpio::GPIO_VALUE_HIGH;
using openxc::gpio::GPIO_VALUE_LOW;
using openxc::gpio::GPIO_DIRECTION_OUTPUT;
using openxc::gpio::GPIO_DIRECTION_INPUT;
using openxc::gpio::GpioValue;

bool openxc::bluetooth::bluetoothConnected() {
    return getGpioValue(BLUETOOTH_STATUS_PORT,
            BLUETOOTH_STATUS_PIN) != GPIO_VALUE_LOW;
}

void openxc::bluetooth::setBluetoothStatus(bool enabled) {
    debug("Turning Bluetooth %s", enabled ? "on" : "off");
    setGpioValue(BLUETOOTH_ENABLE_PORT, BLUETOOTH_ENABLE_PIN,
            enabled ? GPIO_VALUE_HIGH : GPIO_VALUE_LOW);
}

void openxc::bluetooth::initializeBluetooth() {
    debug("Initializing Bluetooth...");
    setGpioDirection(BLUETOOTH_ENABLE_PORT, BLUETOOTH_ENABLE_PIN,
            GPIO_DIRECTION_OUTPUT);
    setGpioDirection(BLUETOOTH_STATUS_PORT, BLUETOOTH_STATUS_PIN,
            GPIO_DIRECTION_INPUT);
    setBluetoothStatus(true);
    debug("Done.");
}