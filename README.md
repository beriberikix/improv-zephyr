# Improv-WiFi for Zephyr

A C implementation of the [Improv Wi-Fi](https://www.improv-wifi.com/) provisioning
protocol for [Zephyr RTOS](https://www.zephyrproject.org/). It lets a browser hand
Wi-Fi credentials to the device over **Web Serial** or **Web Bluetooth**, using native
Zephyr subsystems — Wi-Fi Management (`net_mgmt`), the `wifi_credentials` library, the
UART driver, and the Bluetooth GATT stack — rather than any Arduino library.

It is a port of the official C++ SDK ([improv-wifi/sdk-cpp](https://github.com/improv-wifi/sdk-cpp))
and works with all the Improv installers:

| Installer | Transport | Protocol | Priority |
|-----------|-----------|----------|----------|
| <https://www.improv-wifi.com/serial/> | Web Serial (USB UART) | improv serial | **primary, tested** |
| a raw serial terminal | physical UART | improv serial | secondary |
| <https://www.improv-wifi.com/ble/> | Web Bluetooth | improv BLE GATT | optional |

Tested on **esp32s3_devkitc**, but the code is board-agnostic and uses only portable
Zephyr APIs, so it should run on any Zephyr Wi-Fi target.

## Architecture

```
src/
  improv.c / improv.h        Protocol core: enums, checksum, streaming serial parser,
                             RPC parser, response + serial-frame builders (port of sdk-cpp).
  improv_handler.c/.h        Transport-agnostic state machine + RPC command dispatch.
  wifi_prov.c/.h             net_mgmt connect/scan, DHCP, credential persistence.
  transport_serial.c         Interrupt-driven UART RX/TX (CONFIG_IMPROV_SERIAL).
  transport_ble.c            GATT service + advertising      (CONFIG_IMPROV_BLE).
  main.c                     Boot: settings load, Wi-Fi init, handler, transports.
```

The handler emits logical `(type, payload)` messages; each transport renders them: the
serial transport wraps them in an `IMPROV` frame, the BLE transport notifies the matching
characteristic. Both can be active at once.

## Configuration

Key Kconfig options (see `Kconfig`):

- `CONFIG_IMPROV_SERIAL` (default `y`) — serial transport.
- `CONFIG_IMPROV_BLE` (default `n`) — BLE transport.
- `CONFIG_IMPROV_REDIRECT_URL` — URL returned after successful provisioning (shown as a
  "Next" link). Supports an `{ip}` token; if empty, defaults to `http://<ip>/`; a single
  space suppresses the URL.
- `CONFIG_IMPROV_FIRMWARE_NAME` / `_FIRMWARE_VERSION` / `_DEVICE_NAME` — device-info strings.

## Build & flash

### Option A — build inside an existing Zephyr workspace (recommended here)

If you already have a Zephyr workspace (Zephyr 4.4+) with the esp32 HAL, point the build at
this app directory:

```sh
# from a shell with west available and ZEPHYR_BASE set to your workspace's zephyr/
cd /path/to/improv-wifi
west build -b esp32s3_devkitc/esp32s3/procpu .
west flash
west espressif monitor      # 115200 baud
```

### Option B — standalone west workspace

```sh
west init -l improv-wifi    # run from the directory containing this repo
west update                 # fetches Zephyr + hal_espressif (multi-GB)
west zephyr-export
cd improv-wifi
west build -b esp32s3_devkitc/esp32s3/procpu .
west flash
```

### Add BLE

```sh
west build -b esp32s3_devkitc/esp32s3/procpu . -- -DEXTRA_CONF_FILE=overlay-ble.conf
```

## Provisioning

1. Flash and open the serial monitor; the device boots to the `AUTHORIZED` state.
2. Open <https://www.improv-wifi.com/serial/> in Chrome or Edge, click **Connect**, and
   select the board's serial port.
3. Enter the SSID and password. The device transitions `PROVISIONING → PROVISIONED`,
   obtains a DHCP lease, and returns the redirect URL as the **Next** link.
4. Credentials persist; on reboot the device auto-connects (boots to `PROVISIONED`).

For BLE, open <https://www.improv-wifi.com/ble/> instead and select the advertised
"Zephyr Improv" device.

## Notes

- **Shared console:** improv frames and log output share the console UART. The `IMPROV`
  signature lets the installer extract frames from the surrounding log text (the same
  approach ESPHome uses). If you want a perfectly clean stream, route logging to RTT or a
  second UART and keep the improv transport on the console. Re-requesting the current
  state always re-sends the Wi-Fi settings result, so a rare log/frame interleave is
  self-correcting.
- **Identify:** the IDENTIFY capability is only advertised when a `led0` alias exists.
  esp32s3_devkitc has no simple LED, so identify is a no-op there.
- **ESP32 Wi-Fi + BT coexistence** can be resource constrained; the serial-only image is
  the validated default.

## License

Apache-2.0. Protocol ported from improv-wifi/sdk-cpp (Apache-2.0).
