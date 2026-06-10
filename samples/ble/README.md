# Improv-WiFi BLE sample

Provisions Wi-Fi over a BLE GATT service using the improv BLE protocol,
compatible with the Web Bluetooth installer at <https://www.improv-wifi.com/ble/>.
This sample is BLE-only (the serial transport is disabled).

## Build & flash (esp32s3_devkitc)

From a Zephyr workspace that includes the improv-wifi module (see the top-level
README):

```sh
west build -b esp32s3_devkitc/esp32s3/procpu samples/ble
west flash
```

If the module lives outside your workspace, point the build at it:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu samples/ble \
  -- -DEXTRA_ZEPHYR_MODULES=/abs/path/to/improv-wifi
```

## Provision

1. Open <https://www.improv-wifi.com/ble/> in Chrome or Edge.
2. Select the advertised **Zephyr Improv** device.
3. Enter SSID + password; the device connects and returns the redirect URL.
   Credentials persist and auto-connect on reboot.

> Note: simultaneous Wi-Fi + BT on ESP32 can be resource constrained. If you
> need both transports in one image, enable `CONFIG_IMPROV_SERIAL=y` here too and
> validate carefully.
