# Improv-WiFi serial sample

Provisions Wi-Fi over the console UART using the improv serial protocol,
compatible with the Web Serial installer at <https://www.improv-wifi.com/serial/>.

## Build & flash (esp32s3_devkitc)

From a Zephyr workspace that includes the improv-zephyr module (see the top-level
README for both standalone and external-module setups):

```sh
west build -b esp32s3_devkitc/esp32s3/procpu samples/serial
west flash
```

If the module lives outside your workspace, point the build at it:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu samples/serial \
  -- -DEXTRA_ZEPHYR_MODULES=/abs/path/to/improv-zephyr
```

## Provision

1. Open <https://www.improv-wifi.com/serial/> in Chrome or Edge, click **Connect**.
2. Select the board's **USB-UART** port (e.g. the CP2102 bridge), not the native
   USB-JTAG port — the latter re-enumerates on reset and the browser drops it.
3. Enter SSID + password; the device connects and returns the redirect URL.
   Credentials persist and auto-connect on reboot.
