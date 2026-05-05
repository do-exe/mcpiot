# ESP32-S3 + mcpiot — Complete Reference

> Generated: May 2026  
> IDF: v6.0.1 | Board: ESP32-S3 | Transport: USB-Serial JTAG @ 115200  
> This file documents every hardware peripheral on the ESP32-S3, which ones are
> implemented in this framework, and exactly what to do to add the missing ones.
> **Never dig through datasheets or this conversation again.**

---

## Table of Contents

1. [Project Pipeline (How Everything Works)](#1-project-pipeline)
2. [mcpiot Framework Internals](#2-mcpiot-framework-internals)
3. [Implemented Modules](#3-implemented-modules)
4. [ESP32-S3 Full Peripheral Inventory](#4-esp32-s3-full-peripheral-inventory)
5. [Not-Yet-Implemented — Ready to Add](#5-not-yet-implemented--ready-to-add)
6. [Critical Hardware Facts](#6-critical-hardware-facts)
7. [Known Bugs & Fixes Applied](#7-known-bugs--fixes-applied)
8. [How to Add a New Module (Step-by-Step)](#8-how-to-add-a-new-module-step-by-step)
9. [Build & Flash Cheatsheet](#9-build--flash-cheatsheet)

---

## 1. Project Pipeline

```
project.json          ← ONLY file a developer touches per project
      │
      ▼
python generate.py    ← reads project.json + registry.json
      │  writes ──► main/main.c          (auto-generated, do not edit)
      │         ──► components/modules/CMakeLists.txt  (auto-generated)
      │         ──► mcpiot_repl.py       (auto-generated human REPL)
      ▼
idf.py build          ← compiles everything
idf.py flash          ← flashes to ESP32-S3 on COM3
      ▼
python mcpiot_repl.py       ← human shorthand REPL
  OR
python _wifi_test.py        ← raw JSON-RPC test / AI interface demo
```

### project.json structure
```json
{
  "project": "my_esp32s3_app",
  "board": "esp32s3",
  "transport": { "type": "uart", "uart_num": 0, "baud_rate": 115200,
                 "tx_pin": -1, "rx_pin": -1 },
  "modules": [
    "chip/esp32s3/gpio_module",
    "basic/pwm_module",
    "chip/esp32s3/wifi_module"
  ]
}
```

### registry.json — module catalogue
Located at `components/modules/registry.json`.  
Each entry must have: `id`, `driver`, `header`, `src`, `boards`, `idf_requires`, `methods`.  
Optional per-method: `timeout` (seconds, default 3), `defaults` (for optional params).

---

## 2. mcpiot Framework Internals

### Wire Protocol — JSON-RPC 2.0 over USB-Serial JTAG
- **One JSON object per line** (newline-delimited)
- Request: `{"jsonrpc":"2.0","method":"gpio.read","params":{"pin":2},"id":1}`
- Response: `{"jsonrpc":"2.0","result":{"value":1},"id":1}`
- Error: `{"jsonrpc":"2.0","error":{"code":-32602,"message":"..."},"id":1}`
- Standard JSON-RPC error codes: `-32700` parse error, `-32601` method not found, `-32602` invalid params

### Transport (`components/mcpiot/boards/esp32/mcpiot_transport.c`)
| Constant | Value | Note |
|---|---|---|
| `MCPIOT_LINE_BUF` | 512 bytes | max request JSON length |
| `MCPIOT_RX_TASK_STACK` | **8192 bytes** | must be ≥8 KB when WiFi is included |
| `MCPIOT_USB_RX_BUF` | 512 bytes | USB JTAG driver RX buffer |
| `MCPIOT_USB_TX_BUF` | 512 bytes | USB JTAG driver TX buffer |
| TX mutex timeout | 200 ms | prevents concurrent sends |

> ⚠️ **Stack size is critical.** If any handler calls IDF WiFi / BLE APIs, 8192 is the minimum.  
> 4096 causes a silent stack overflow → method returns nothing → Python times out.

### RPC Core (`components/mcpiot/core/`)
| File | Role |
|---|---|
| `mcpiot_rpc.c` | parses JSON, dispatches to registered handlers |
| `mcpiot_registry.c` | stores handler table (max 32 methods, see `MCPIOT_MAX_RPC_METHODS`) |
| `mcpiot_event.c` | push-event stub (not yet implemented) |
| `mcpiot.c` | init entry point called from `main.c` |
| `cJSON.c` | bundled cJSON library |

### Handler signature
```c
// Every handler must match this signature:
typedef char *(*mcpiot_handler_t)(cJSON *params, int id);
// Must return a heap-allocated JSON string — framework calls free() on it.
```

### Module driver struct
```c
typedef struct {
    const char *type;           // e.g. "wifi"
    void (*init)(void);
    void (*register_methods)(void);
    void (*register_events)(void);
    int method_count;
} mcpiot_module_driver_t;
```

---

## 3. Implemented Modules

### 3.1 `chip/esp32s3/gpio_module`
**IDF component:** `esp_driver_gpio`

| Method | Params | Returns |
|---|---|---|
| `gpio.read` | `{"pin": N}` | `{"value": 0\|1}` |
| `gpio.write` | `{"pin": N, "value": 0\|1}` | `{"ok": true}` |

- Uses `gpio_get_level()` / `gpio_set_level()`
- `gpio.read` auto-configures pin as INPUT with pull-up if not already set
- `gpio.write` auto-configures pin as OUTPUT if not already set

### 3.2 `basic/pwm_module`
**IDF component:** `esp_driver_ledc`  
Uses LEDC (LED Control) peripheral — the standard ESP-IDF PWM API.

| Method | Params | Returns |
|---|---|---|
| `pwm.set` | `{"pin": N, "duty": 0-100, "freq": Hz}` | `{"ok":true,"pin":N,"duty":50.0,"freq":1000}` |

- `freq` is optional (default 1000 Hz)
- `duty=0` → pin held LOW, `duty=100` → pin held HIGH
- Uses `LEDC_LOW_SPEED_MODE`, channel assigned automatically per pin
- Max simultaneous PWM channels: **8** (LEDC hardware limit on ESP32-S3)

### 3.3 `chip/esp32s3/wifi_module`
**IDF components:** `esp_wifi`, `esp_netif`, `nvs_flash`

| Method | Params | Returns | Timeout |
|---|---|---|---|
| `wifi.scan` | `{}` | `[{ssid, rssi, auth}]` | 10 s |
| `wifi.connect` | `{"ssid":"...", "password":"..."}` | `{"ok":true,"ip":"..."}` | 20 s |
| `wifi.status` | `{}` | `{"connected":bool,"ip":"...","ssid":"...","rssi":N}` | 3 s |
| `wifi.disconnect` | `{}` | `{"ok":true}` | 3 s |

**Key implementation details:**
- WiFi stack initialised **eagerly at boot** in `wifi_init()` — NOT lazily on first call
- `password` param is optional; omit or pass `""` for open networks
- Auth modes: `WIFI_AUTH_OPEN` when password is empty, else `WIFI_AUTH_WPA2_PSK`
- Connect uses `EventGroup` bits `WIFI_CONNECTED_BIT` / `WIFI_FAIL_BIT` with 15 s timeout
- `auth` field in scan results: `"OPEN"`, `"WEP"`, `"WPA_PSK"`, `"WPA2_PSK"`,  
  `"WPA_WPA2_PSK"`, `"WPA2_ENTERPRISE"`, `"WPA3_PSK"`, `"WPA2_WPA3_PSK"`,  
  `"WAPI_PSK"`, `"OWE"` (index 0–9)
- NVS flash is initialised once; gracefully handles `ESP_ERR_NVS_NO_FREE_PAGES`

---

## 4. ESP32-S3 Full Peripheral Inventory

> Official Espressif docs: https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/

### Digital I/O
| Peripheral | Count | IDF Component | Implemented? |
|---|---|---|---|
| GPIO | 45 pins (0-21, 26-48) | `esp_driver_gpio` | ✅ gpio_module |
| RTC GPIO | 22 pins (0-21) — deep-sleep-safe | `esp_driver_gpio` | ❌ not wrapped |
| RTCIO hold | keep output level in deep sleep | `esp_driver_gpio` | ❌ |

### Analog
| Peripheral | Count | IDF Component | Implemented? |
|---|---|---|---|
| ADC1 | 10 channels (GPIO 1-10) | `esp_adc` | ❌ |
| ADC2 | 10 channels (GPIO 11-20) — conflicts with WiFi | `esp_adc` | ❌ |
| DAC | 2 channels (GPIO 17, 18) — 8-bit | `esp_driver_dac` | ❌ |
| Analog Comparator | 1 | `esp_driver_ana_cmpr` | ❌ |

> ⚠️ ADC2 is **disabled when WiFi is active** — do not use ADC2 channels in wifi projects.  
> Use ADC1 (GPIO 1–10) instead.

### PWM / Timers
| Peripheral | Count | IDF Component | Implemented? |
|---|---|---|---|
| LEDC (PWM) | 8 channels, 4 timers | `esp_driver_ledc` | ✅ pwm_module |
| MCPWM | 2 units × 3 pairs = 6 PWM outputs | `esp_driver_mcpwm` | ❌ |
| General Purpose Timers (GPTIMER) | 4 × 54-bit | `esp_driver_gptimer` | ❌ |
| RTC timer | 1 | `esp_driver_gptimer` | ❌ |
| Systimer | 2 counters (used by IDF internally) | internal | — |

### Serial / Bus
| Peripheral | Count | IDF Component | Implemented? |
|---|---|---|---|
| UART | 3 (UART0 = console/REPL, UART1, UART2) | `driver` / `esp_driver_uart` | ❌ |
| I2C | 2 | `esp_driver_i2c` | ❌ |
| SPI (Master) | 4 (SPI0/1 internal flash, SPI2, SPI3 free) | `driver` | ❌ |
| I2S | 2 (audio/PDM/TDM) | `esp_driver_i2s` | ❌ |
| I3C | 1 | `esp_driver_i3c` | ❌ |
| TWAI (CAN 2.0) | 2 | `driver` | ❌ |
| SDIO slave | 1 | `sdmmc` | ❌ |
| USB OTG FS | 1 (GPIO 19, 20) | `usb` | ❌ |
| USB Serial/JTAG | 1 — **used by mcpiot transport** | `driver/usb_serial_jtag` | ✅ (transport) |

### Wireless
| Peripheral | IDF Component | Implemented? |
|---|---|---|
| Wi-Fi 4 (802.11 b/g/n, 2.4 GHz) | `esp_wifi`, `esp_netif` | ✅ wifi_module |
| Bluetooth 5.0 LE | `bt` | ❌ |
| Bluetooth Classic (BR/EDR) | `bt` | ❌ (ESP32-S3 has no BR/EDR — only BLE) |
| ESP-NOW | `esp_wifi` (no AP join needed) | ❌ |
| Wi-Fi AP mode | `esp_wifi` | ❌ (only STA mode implemented) |
| Wi-Fi AP+STA coexist | `esp_wifi` | ❌ |
| SmartConfig / WiFi provisioning | `wifi_provisioning` | ❌ |

### Camera / Image
| Peripheral | IDF Component | Implemented? |
|---|---|---|
| DVP Camera interface | `esp_driver_cam` | ❌ |
| ISP (Image Signal Processor) | `esp_driver_isp` | ❌ |
| JPEG encoder/decoder (HW) | `esp_driver_jpeg` | ❌ |
| LCD (SPI/I80/RGB) | `esp_lcd` | ❌ |

### DMA / Memory
| Peripheral | IDF Component | Implemented? |
|---|---|---|
| GDMA (General DMA) | `esp_driver_dma` | internal, used by drivers |
| BitScrambler | `esp_driver_bitscrambler` | ❌ |
| PSRAM (Octal/Quad, up to 8 MB) | `esp_psram` | ❌ (auto-enabled by sdkconfig) |

### Security / Crypto
| Peripheral | IDF Component | Implemented? |
|---|---|---|
| SHA accelerator | `mbedtls` | ❌ (exposed via TLS stack) |
| AES accelerator | `mbedtls` | ❌ |
| RSA accelerator | `mbedtls` | ❌ |
| ECC accelerator | `mbedtls` | ❌ |
| Digital Signature peripheral | `esp_secure_cert_mgr` | ❌ |
| eFuse (MAC, keys, secure boot) | `efuse` | ❌ |
| Flash Encryption | sdkconfig | ❌ |
| Secure Boot v2 | sdkconfig | ❌ |

### System
| Peripheral | IDF Component | Implemented? |
|---|---|---|
| NVS (non-volatile storage) | `nvs_flash` | ✅ (used internally by wifi_module) |
| Partition table | `esp_partition` | ✅ (default layout) |
| OTA updates | `app_update` | ❌ |
| Deep sleep / light sleep | `esp_sleep` | ❌ |
| Watchdog (TWDT / IWDT) | `esp_timer` | ✅ (auto, IDF default) |
| Event loop | `esp_event` | ✅ (used by wifi_module) |
| Console (REPL via UART) | `console` | ❌ (mcpiot uses its own REPL) |
| ESP-IDF heap caps | `heap` | ✅ (IDF default) |
| ULP coprocessor | internal | ❌ |
| Touch sensor (14 channels) | `driver` | ❌ |
| Hall sensor | removed in S3 | N/A |
| Temp sensor (internal) | `driver` | ❌ |

### Parallel / Display Bus
| Peripheral | IDF Component | Implemented? |
|---|---|---|
| Parallel IO (PARLIO) | `esp_driver_parlio` | ❌ |
| LCD I80 bus | `esp_lcd` | ❌ |
| LCD RGB bus | `esp_lcd` | ❌ |

---

## 5. Not-Yet-Implemented — Ready to Add

These are the most practically useful ones. Each row shows the exact `idf_requires` to put in `registry.json`.

| Module ID (suggestion) | Methods | `idf_requires` | Notes |
|---|---|---|---|
| `chip/esp32s3/adc_module` | `adc.read {pin, attenuation}` | `["esp_adc"]` | ADC1 only when WiFi active; returns mV |
| `chip/esp32s3/dac_module` | `dac.set {channel, voltage_mv}` | `["esp_driver_dac"]` | GPIO 17 = ch0, GPIO 18 = ch1; 8-bit (0-3300 mV) |
| `chip/esp32s3/uart_module` | `uart.write {uart, data}`, `uart.read {uart}` | `["driver"]` | UART1/UART2 free; UART0 is console |
| `chip/esp32s3/i2c_module` | `i2c.scan`, `i2c.read {addr,reg,len}`, `i2c.write {addr,reg,data}` | `["esp_driver_i2c"]` | Two I2C controllers |
| `chip/esp32s3/spi_module` | `spi.transfer {cs, data}` | `["driver"]` | SPI2 (HSPI) and SPI3 (VSPI) are free |
| `chip/esp32s3/touch_module` | `touch.read {pad}` | `["driver"]` | 14 touch pads; GPIO 1-14 |
| `chip/esp32s3/temp_module` | `temp.read` | `["driver"]` | Internal die temperature sensor; ±1°C |
| `chip/esp32s3/ble_module` | `ble.scan`, `ble.advertise {name}`, `ble.notify {handle, data}` | `["bt","nvs_flash"]` | BLE 5.0 only (no Classic BT on S3) |
| `chip/esp32s3/sleep_module` | `sleep.deep {wakeup_pin}`, `sleep.light {duration_ms}` | `["esp_sleep"]` | Deep sleep current ~7 µA |
| `chip/esp32s3/nvs_module` | `nvs.set {key, value}`, `nvs.get {key}`, `nvs.del {key}` | `["nvs_flash"]` | Persistent key-value store across reboots |
| `chip/esp32s3/ota_module` | `ota.start {url}`, `ota.status` | `["app_update","esp_https_ota"]` | OTA via HTTPS URL |
| `chip/esp32s3/wifi_ap_module` | `wifi.ap_start {ssid,password}`, `wifi.ap_stop` | `["esp_wifi","esp_netif","nvs_flash"]` | Soft-AP mode; useful for provisioning |
| `chip/esp32s3/espnow_module` | `espnow.send {mac, data}`, `espnow.recv` | `["esp_wifi"]` | No router needed; peer-to-peer up to 250 bytes |
| `basic/servo_module` | `servo.set {pin, angle}` | `["esp_driver_ledc"]` | Built on LEDC; 50 Hz, 1-2 ms pulse |
| `basic/tone_module` | `tone.play {pin, freq, duration_ms}` | `["esp_driver_ledc"]` | Buzzer tones; uses LEDC |
| `basic/encoder_module` | `encoder.read {pin_a, pin_b}` | `["esp_driver_pcnt"]` | Rotary encoder via PCNT hardware counter |
| `basic/neopixel_module` | `neopixel.set {pin, index, r, g, b}` | `["esp_driver_rmt"]` | WS2812 via RMT peripheral |

---

## 6. Critical Hardware Facts

### GPIO
- **GPIO 0** — strapping pin: HIGH = normal boot, LOW = download mode. Safe to use as output after boot.
- **GPIO 3** — strapping pin.
- **GPIO 45** — strapping pin (VDD_SPI voltage select on some boards).
- **GPIO 46** — strapping pin (ROM messages).
- **GPIO 19, 20** — used by USB OTG. If you use `usb_serial_jtag` transport (default), these are already consumed.
- **GPIO 26-32** — connected to internal flash (QSPI). **Do not use.**
- **GPIO 33-37** — connected to PSRAM (if present). **Do not use if PSRAM enabled.**
- **GPIO 38-48** — safe general-purpose pins.
- Safe output pins for general use: **1-18, 21, 38-48** (avoiding flash/PSRAM/USB).

### ADC
- ADC1 channels: GPIO 1–10 → ADC1_CH0–CH9
- ADC2 channels: GPIO 11–20 → ADC2_CH0–CH9
- **ADC2 is entirely disabled while Wi-Fi radio is active** (hardware limitation, not a bug).
- Default attenuation: `ADC_ATTEN_DB_12` → 0–3100 mV range.
- Internal reference ~1100 mV; always use `esp_adc_cal` for calibrated voltage.

### LEDC (PWM)
- 8 independent channels (0–7), 4 independent timers.
- Can output any frequency from ~1 Hz to ~40 MHz depending on resolution bits.
- All channels share the same base clock (`APB_CLK = 80 MHz`).
- Resolution = `log2(80_000_000 / freq)` bits (e.g. 1 kHz → 16 bits).

### Wi-Fi
- **2.4 GHz only** — no 5 GHz support on ESP32-S3.
- Max TCP throughput: ~20 Mbps.
- Wi-Fi and BLE can coexist via `esp_coex` but with reduced throughput.
- ADC2 is unusable while Wi-Fi TX is active.
- `nvs_flash_init()` must be called before `esp_wifi_init()`.

### BLE
- Bluetooth 5.0 LE only — no Classic BT (BR/EDR).
- Max BLE advertising PDU: 37 bytes (legacy) / 255 bytes (extended).
- BLE and Wi-Fi coexistence is supported but adds ~10 ms latency to Wi-Fi.

### Memory
| Region | Size | Notes |
|---|---|---|
| Internal SRAM | 512 KB | split across DRAM/IRAM |
| RTC SRAM | 16 KB FAST + 8 KB SLOW | survives deep sleep |
| PSRAM (external) | up to 8 MB (Octal) | requires sdkconfig `CONFIG_SPIRAM_*` |
| Flash | up to 16 MB (Quad/Octal) | default partition table uses 4 MB |

### Clocks
| Clock | Value |
|---|---|
| CPU | 240 MHz (default), configurable 80/160/240 |
| APB | 80 MHz |
| RTC slow | 32 kHz (internal) or 32.768 kHz external crystal |
| XTAL | 40 MHz on most modules |

---

## 7. Known Bugs & Fixes Applied

| # | Bug | Root Cause | Fix |
|---|---|---|---|
| 1 | `NameError: name 'ssid' is not defined` in mcpiot_repl.py | HELP strings passed through `f"..."` — `{ssid}` evaluated as Python variable | `escape_fstring()` in generate.py escapes `{` → `{{` before emitting HELP strings |
| 2 | `wifi.scan` silently times out, returns nothing | `MCPIOT_RX_TASK_STACK = 4096` — stack overflow during `esp_wifi_scan_start()` IDF call | Increased to **8192** in `mcpiot_transport.c` |
| 3 | `wifi.scan` REPL timeout after 3 s | `send()` had hard-coded 3 s timeout; scan takes 4–8 s | `METHOD_TIMEOUTS` dict in generated REPL; `send(timeout=t)` |
| 4 | Stale response bleed — wrong reply received | `send()` returned first JSON line regardless of `id`; cold-boot lazy WiFi init caused >12 s response delay that landed in next call's window | Added `s.reset_input_buffer()` before write + `resp.get("id") == req_id` ID validation |
| 5 | Open-network connect fails | `password` param was required; `WIFI_AUTH_WPA2_PSK` set unconditionally | Password optional (`defaults: {"password": ""}`); `WIFI_AUTH_OPEN` used when `strlen(pass) == 0` |
| 6 | `cp1252` encode error in REPL subprocess | `←` (U+2190) in prompt string can't encode in Windows cp1252 | Replaced arrow with `<-` ASCII; pass `PYTHONUTF8=1` env var in `_wifi_test.py` subprocess |
| 7 | `IndentationError` in generated mcpiot_repl.py | Stale `L.append("                pass")` + `L.append("    return None")` lines left in `generate.py` template after an edit | Removed both stale lines from generate.py |
| 8 | Scan result printed as Python list `repr` | `pretty()` in REPL only handled `dict`, not `list` | `isinstance(result, (dict, list))` check + `json.dumps(indent=2, ensure_ascii=False)` |
| 9 | SSID with spaces parsed as two args | `parse_input()` used `text.split()` | Changed to `shlex.split(text)` in generate.py; `import shlex` added to generated REPL |

---

## 8. How to Add a New Module (Step-by-Step)

### Example: adding `adc.read`

**Step 1 — Create the C files**
```
components/modules/chip/esp32s3/adc_module.h
components/modules/chip/esp32s3/adc_module.c
```

`adc_module.h`:
```c
#pragma once
#include "mcpiot_module.h"
extern const mcpiot_module_driver_t adc_module_driver;
```

`adc_module.c` — follow the exact same pattern as `wifi_module.c`:
```c
#include "adc_module.h"
#include "mcpiot_rpc.h"
#include "cJSON.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

static char *handle_adc_read(cJSON *params, int id) { ... }

static void adc_init(void) { /* init oneshot unit */ }
static void adc_register_methods(void) {
    mcpiot_rpc_register("adc.read", handle_adc_read);
}
static void adc_register_events(void) {}

const mcpiot_module_driver_t adc_module_driver = {
    .type = "adc",
    .init = adc_init,
    .register_methods = adc_register_methods,
    .register_events = adc_register_events,
    .method_count = 1,
};
```

**Step 2 — Add to registry.json**
```json
{
  "id": "chip/esp32s3/adc_module",
  "driver": "adc_module_driver",
  "header": "adc_module.h",
  "src": "chip/esp32s3/adc_module.c",
  "boards": ["esp32s3"],
  "idf_requires": ["esp_adc"],
  "methods": [
    {
      "name": "adc.read",
      "params": { "pin": "int" },
      "help": "adc.read <pin>  →  {pin, mv}"
    }
  ]
}
```

**Step 3 — Add to project.json modules list**
```json
"modules": [
  "chip/esp32s3/gpio_module",
  "basic/pwm_module",
  "chip/esp32s3/wifi_module",
  "chip/esp32s3/adc_module"   ← add this
]
```

**Step 4 — Regenerate + build**
```powershell
python generate.py
idf.py build
idf.py flash
```

That's it. `main.c`, `CMakeLists.txt`, and `mcpiot_repl.py` are all regenerated automatically.

---

## 9. Build & Flash Cheatsheet

```powershell
# Activate IDF environment (run once per terminal session)
D:\Espressif\.espressif\v6.0.1\esp-idf\export.ps1

# Or use the IDF shortcut if configured:
get-idf      # (if alias is set up)

# Full build
idf.py build

# Flash (auto-detects COM port or specify)
idf.py -p COM3 flash

# Flash + open serial monitor
idf.py -p COM3 flash monitor

# Just monitor (Ctrl+] to exit)
idf.py -p COM3 monitor

# Erase flash completely (use when NVS is corrupt)
idf.py -p COM3 erase-flash

# menuconfig (GUI for sdkconfig)
idf.py menuconfig

# Regenerate main.c / CMakeLists.txt / mcpiot_repl.py
python generate.py

# Run human REPL
python mcpiot_repl.py

# Run two-phase WiFi hardware test (raw JSON + REPL)
python _wifi_test.py

# Check build size
idf.py size

# Show all registered IDF components
idf.py --list-targets
```

### COM Port on Windows
```powershell
# List all COM ports
Get-PnpDevice -Class Ports | Select-Object Name, Status

# Device: VID=0x303A PID=0x1001 → Espressif USB-Serial/JTAG → COM3
```

### sdkconfig defaults (`sdkconfig.defaults`)
Anything set here survives `idf.py fullclean`. Put permanent settings here, not in `sdkconfig` directly.

---

*End of reference. Update this file when new modules are added or bugs are found.*
