# Building the Raspberry Pi Pico W Companion Firmwares (USB + WiFi / BLE / All)

This guide explains how to compile the three combined‑transport companion
firmware variants for the **Raspberry Pi Pico W** (RP2040 + onboard CYW43 WiFi/BLE,
with an external SX1262 LoRa module):

| UF2 output | PlatformIO environment | Transports enabled |
|------------|------------------------|--------------------|
| `PicoW_companion_usb_wifi.uf2` | `PicoW_companion_radio_usb_wifi` | USB serial + WiFi (TCP) |
| `PicoW_companion_usb_ble.uf2`  | `PicoW_companion_radio_usb_ble`  | USB serial + Bluetooth LE |
| `PicoW_companion_all.uf2`      | `PicoW_companion_radio_all`      | USB serial + WiFi + Bluetooth LE |

All three are companion‑radio builds (binary frame protocol used by the MeshCore
apps), defined in [`variants/rpi_picow/platformio.ini`](variants/rpi_picow/platformio.ini).

---

## 1. Prerequisites

- **macOS / Linux / Windows** with **Python 3.9+**.
- **Git** (the project pulls its Arduino core/toolchain automatically on first build).
- Internet access on the first build (PlatformIO downloads the `platform-raspberrypi`
  Arduino‑Pico toolchain, RadioLib, etc.).
- No IDE required — everything below uses the PlatformIO CLI.

> The RP2040 toolchain, `picotool`, and the CYW43 WiFi/BLE stack are all fetched and
> managed by PlatformIO. You do **not** need to install the Pico SDK separately.

---

## 2. Install PlatformIO CLI (one time)

Install PlatformIO into an isolated virtual environment so it never conflicts with
system Python:

```bash
python3 -m venv ~/.pio-venv
~/.pio-venv/bin/pip install --upgrade pip platformio
```

Add it to your PATH for the current shell (repeat in new shells, or add to your
shell profile):

```bash
export PATH="$HOME/.pio-venv/bin:$PATH"
pio --version        # should print: PlatformIO Core, version 6.x
```

---

## 3. Get the source

```bash
cd ~/Desktop            # or wherever you keep projects
git clone https://github.com/meshcore-dev/MeshCore
cd MeshCore
```

(If you already have the repo with these variants, just `cd` into it.)

---

## 4. Configure WiFi credentials (WiFi and All builds only)

The WiFi transport uses **build‑time credentials** (baked into the firmware),
matching the existing MeshCore pattern. Edit
[`variants/rpi_picow/platformio.ini`](variants/rpi_picow/platformio.ini) and set your
network in the relevant environment(s):

```ini
  -D WIFI_SSID='"your-network-name"'
  -D WIFI_PWD='"your-network-password"'
```

- Change these in **`[env:PicoW_companion_radio_usb_wifi]`** for the WiFi build.
- Change them in **`[env:PicoW_companion_radio_all]`** for the combined build.
- The BLE‑only build (`PicoW_companion_radio_usb_ble`) has no WiFi settings.

> Keep the exact quoting: `'"value"'` (single quotes around double quotes). To change
> the WiFi TCP port (default **5000**), add `-D TCP_PORT=xxxx` to the same env.

The BLE builds use a fixed pairing pin define `-D BLE_PIN_CODE=123456`. See the
[Security note](#8-security-notes) below — on RP2040 this pin is currently **not
enforced**.

---

## 5. Build

Run these from the repository root with PlatformIO on your PATH
(`export PATH="$HOME/.pio-venv/bin:$PATH"`).

### Build one variant at a time

```bash
# USB + WiFi
pio run -e PicoW_companion_radio_usb_wifi

# USB + Bluetooth LE
pio run -e PicoW_companion_radio_usb_ble

# USB + WiFi + Bluetooth LE (everything)
pio run -e PicoW_companion_radio_all
```

### Build all three in one command

```bash
pio run \
  -e PicoW_companion_radio_usb_wifi \
  -e PicoW_companion_radio_usb_ble \
  -e PicoW_companion_radio_all
```

Each successful build ends with a size summary and generates a ready‑to‑flash UF2.
The Arduino‑Pico core produces the `.uf2` automatically via `picotool`
(no extra conversion step is needed for RP2040).

---

## 6. Where the UF2 files land

After a build, the firmware for each environment is here:

```
.pio/build/PicoW_companion_radio_usb_wifi/firmware.uf2
.pio/build/PicoW_companion_radio_usb_ble/firmware.uf2
.pio/build/PicoW_companion_radio_all/firmware.uf2
```

Optionally copy them to a single `out/` folder with friendly names:

```bash
mkdir -p out
cp .pio/build/PicoW_companion_radio_usb_wifi/firmware.uf2 out/PicoW_companion_usb_wifi.uf2
cp .pio/build/PicoW_companion_radio_usb_ble/firmware.uf2  out/PicoW_companion_usb_ble.uf2
cp .pio/build/PicoW_companion_radio_all/firmware.uf2      out/PicoW_companion_all.uf2
ls -lh out/PicoW_companion_*.uf2
```

### (Optional) Official versioned artifacts via `build.sh`

The repo's build wrapper adds a version/commit suffix and copies `.uf2` + `.bin`
into `out/`:

```bash
export FIRMWARE_VERSION=v1.0.0
sh build.sh build-firmware PicoW_companion_radio_usb_wifi
sh build.sh build-firmware PicoW_companion_radio_usb_ble
sh build.sh build-firmware PicoW_companion_radio_all
# → out/PicoW_companion_radio_all-v1.0.0-<sha>.uf2, etc.
```

---

## 7. Flash to the Pico W

1. Hold the **BOOTSEL** button on the Pico W and plug it into USB.
2. It mounts as a mass‑storage drive named **`RPI-RP2`**.
3. Drag‑and‑drop the chosen `.uf2` onto that drive (or `cp firmware.uf2 /Volumes/RPI-RP2/` on macOS).
4. The board reboots automatically into the new firmware.

Alternatively, with the board in BOOTSEL mode you can use PlatformIO's uploader:

```bash
pio run -e PicoW_companion_radio_all -t upload
```

---

## 8. Security notes

- **WiFi credentials are compiled into the firmware.** Anyone with the `.uf2` /
  `.bin` can extract them. Don't share built binaries that contain real credentials.
- **BLE is currently unencrypted / unpaired on the Pico W.** The RP2040 BLE interface
  registers its Nordic UART Service characteristics with no security requirement, so
  the `BLE_PIN_CODE` value is accepted for compatibility but **not enforced** (unlike
  the ESP32/nRF52 builds, which require MITM passkey pairing). Any BLE client in range
  can connect. Treat this as a functional first version and keep the device in a
  trusted environment until pairing is added.

---

## 9. Clean / rebuild

```bash
# Clean a single environment
pio run -e PicoW_companion_radio_all -t clean

# Force a full re-resolve of libraries and rebuild
pio run -e PicoW_companion_radio_all -t clean && pio run -e PicoW_companion_radio_all
```

---

## 10. Troubleshooting

| Symptom | Cause / Fix |
|---------|-------------|
| `pio: command not found` | PlatformIO not on PATH. Run `export PATH="$HOME/.pio-venv/bin:$PATH"`. |
| `NotPlatformIOProjectError: platformio.ini ... not found` | You're not in the repo root. `cd` back into the `MeshCore` directory. |
| `This library needs Bluetooth enabled` / `BTstack` errors in a WiFi‑only build | The BLE stack must stay out of non‑BLE builds. This is handled by `lib_ignore = BLE, BTstack` and per‑env source filters in the WiFi env — don't remove them. |
| BLE builds fail without `PIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` | The `usb_ble` and `all` envs must keep `-D PIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH`; it links the CYW43 Bluetooth stack. |
| First build is very slow | Expected — PlatformIO downloads the Arduino‑Pico toolchain/framework once, then caches it. |
| WiFi never connects | Check `WIFI_SSID` / `WIFI_PWD` quoting (`'"value"'`) and that the network is 2.4 GHz (the Pico W has no 5 GHz radio). |

---

## 11. Quick reference

```bash
# One-time setup
python3 -m venv ~/.pio-venv
~/.pio-venv/bin/pip install --upgrade pip platformio
export PATH="$HOME/.pio-venv/bin:$PATH"

# From the MeshCore repo root: edit WiFi creds in variants/rpi_picow/platformio.ini, then:
pio run -e PicoW_companion_radio_usb_wifi \
        -e PicoW_companion_radio_usb_ble \
        -e PicoW_companion_radio_all

# UF2s: .pio/build/<env>/firmware.uf2
# Flash: hold BOOTSEL, plug in, drop the .uf2 on the RPI-RP2 drive
```
