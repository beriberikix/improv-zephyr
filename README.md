# Improv-WiFi for Zephyr

A reusable [Zephyr](https://www.zephyrproject.org/) **module** implementing the
[Improv Wi-Fi](https://www.improv-wifi.com/) provisioning protocol in C. It lets a
browser hand Wi-Fi credentials to the device over **Web Serial** or **Web Bluetooth**,
using native Zephyr subsystems — Wi-Fi Management (`net_mgmt`), the `wifi_credentials`
library, the UART driver, and the Bluetooth GATT stack — rather than any Arduino library.

Ported from the official C++ SDK ([improv-wifi/sdk-cpp](https://github.com/improv-wifi/sdk-cpp)).
Works with every Improv installer:

| Installer | Transport | Protocol |
|-----------|-----------|----------|
| <https://www.improv-wifi.com/serial/> | Web Serial (USB UART) | improv serial |
| a raw serial terminal | physical UART | improv serial |
| <https://www.improv-wifi.com/ble/> | Web Bluetooth | improv BLE GATT |

Validated on **esp32s3_devkitc**, but the code is board-agnostic and uses only portable
Zephyr APIs, so it should run on any Zephyr Wi-Fi target.

## Layout

```
improv-wifi/                  ← Zephyr module
├── zephyr/module.yml         Module manifest (build + Kconfig entry points)
├── CMakeLists.txt            zephyr_library(), gated on CONFIG_IMPROV_WIFI
├── Kconfig                   CONFIG_IMPROV_WIFI / _SERIAL / _BLE / _REDIRECT_URL / ...
├── include/improv/
│   ├── improv.h              Protocol core: enums, checksum, parser, builders
│   └── improv_wifi.h         Public entry point: improv_wifi_init()
├── src/
│   ├── improv.c              Protocol core (port of sdk-cpp)
│   ├── improv_handler.c      Transport-agnostic state machine + RPC dispatch
│   ├── improv_wifi.c         improv_wifi_init() wiring
│   ├── wifi_prov.c           net_mgmt connect/scan, DHCP, credential persistence
│   ├── transport_serial.c    Interrupt-driven UART RX/TX (CONFIG_IMPROV_SERIAL)
│   └── transport_ble.c       GATT service + advertising      (CONFIG_IMPROV_BLE)
├── samples/
│   ├── serial/               JS Web Serial example app
│   └── ble/                  Web Bluetooth example app
└── west.yml                  Manifest for standalone development
```

## Using it as a module in another project

Add the module to your application's `west.yml`:

```yaml
manifest:
  projects:
    - name: improv-wifi
      url: https://github.com/beriberikix/improv-wifi
      revision: main
      path: modules/improv-wifi
```

Run `west update`. Zephyr auto-discovers the module via `zephyr/module.yml`. Then in your
app's `prj.conf` enable the protocol and the Wi-Fi/transport options it needs (see
`samples/serial/prj.conf` for the full list):

```ini
CONFIG_IMPROV_WIFI=y
CONFIG_IMPROV_SERIAL=y        # and/or CONFIG_IMPROV_BLE=y
CONFIG_WIFI=y
CONFIG_WIFI_CREDENTIALS=y
# ... networking + settings, as in the samples
```

and call the single entry point from your `main()`:

```c
#include <improv/improv_wifi.h>

int main(void)
{
	improv_wifi_init();   /* loads stored creds, starts Wi-Fi + transports */
	return 0;
}
```

`improv_wifi_init()` brings up whichever transports are enabled in Kconfig. For lower-level
use, `<improv/improv.h>` exposes the pure protocol core (parser/builders) with no Zephyr
dependency.

### Key Kconfig options

- `CONFIG_IMPROV_WIFI` — enable the module.
- `CONFIG_IMPROV_SERIAL` (default `y`) / `CONFIG_IMPROV_BLE` (default `n`) — transports;
  both may be enabled together.
- `CONFIG_IMPROV_REDIRECT_URL` — URL returned after provisioning (shown as a "Next" link);
  supports an `{ip}` token; empty → `http://<ip>/`; a single space suppresses it.
- `CONFIG_IMPROV_FIRMWARE_NAME` / `_FIRMWARE_VERSION` / `_DEVICE_NAME` — device-info strings.

## Building the samples

### Standalone (this repo as the west manifest)

```sh
west init -l improv-wifi      # run from the directory containing this repo
west update                   # fetches Zephyr + hal_espressif
west zephyr-export
west build -b esp32s3_devkitc/esp32s3/procpu improv-wifi/samples/serial
west flash
```

### Inside an existing Zephyr workspace

If the module is not in your workspace manifest, point the build at it directly:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu samples/serial \
  -- -DEXTRA_ZEPHYR_MODULES=$(pwd)
west flash
```

Swap `samples/serial` for `samples/ble` for the Web Bluetooth example. See each sample's
README for provisioning steps.

## Notes

- **Shared console:** improv serial frames and log output share the console UART; the
  `IMPROV` signature lets the installer extract frames from the log text (the approach
  ESPHome uses). For a perfectly clean stream, route logging to RTT or a second UART.
- **Port choice:** with the Web Serial installer, select the USB-UART bridge port, not a
  native USB-JTAG/CDC port — the latter re-enumerates on reset and the browser drops it.
- **Wi-Fi security:** connect uses `WIFI_SECURITY_TYPE_PSK` (WPA2). A WPA3-only AP would
  need `WIFI_SECURITY_TYPE_SAE`/`WPA_AUTO_PERSONAL`.
- **Identify:** advertised only when a `led0` alias exists; esp32s3_devkitc has none.
- **ESP32 Wi-Fi + BT coexistence** can be resource constrained; serial-only is the
  validated default.

## License

Apache-2.0. Protocol ported from improv-wifi/sdk-cpp (Apache-2.0).
