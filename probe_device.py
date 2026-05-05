import serial
import serial.tools.list_ports
import time
import json

BAUD = 115200

# ESP32-S3 USB-Serial/JTAG VID:PID
ESP32S3_VID = 0x303A
ESP32S3_PID = 0x1001

def find_esp32_port():
    """Auto-detect ESP32-S3 USB-Serial/JTAG port by VID:PID, like esptool does."""
    ports = list(serial.tools.list_ports.comports())
    # First pass: exact VID:PID match
    for p in ports:
        if p.vid == ESP32S3_VID and p.pid == ESP32S3_PID:
            print(f"[auto-detect] Found ESP32-S3 on {p.device} ({p.description})")
            return p.device
    # Second pass: description contains 'USB Serial' or 'CP210' etc.
    for p in ports:
        if any(kw in (p.description or '').lower() for kw in ('usb serial', 'cp210', 'ch340', 'ftdi')):
            print(f"[auto-detect] Best guess: {p.device} ({p.description})")
            return p.device
    if ports:
        print(f"[auto-detect] No ESP32 found. Available ports:")
        for p in ports:
            print(f"  {p.device}  VID={hex(p.vid) if p.vid else '?'}  {p.description}")
        raise RuntimeError("Could not auto-detect ESP32 port. Connect the device and try again.")
    raise RuntimeError("No serial ports found. Is the device connected?")

PORT = find_esp32_port()

def send_rpc(s, method, params=None, req_id=1):
    req = {"jsonrpc": "2.0", "method": method, "id": req_id}
    if params is not None:
        req["params"] = params
    payload = (json.dumps(req) + "\r\n").encode()
    s.write(payload)
    time.sleep(0.4)
    out = b''
    deadline = time.time() + 2
    while time.time() < deadline:
        chunk = s.read(s.in_waiting or 1)
        if chunk:
            out += chunk
            if b'\n' in out:
                break
        time.sleep(0.05)
    raw = out.decode('utf-8', errors='replace').strip()
    for line in raw.splitlines():
        if line.strip().startswith('{'):
            try:
                return json.loads(line)
            except Exception:
                pass
    return None

print(f"=== GPIO Tests on {PORT} @ {BAUD} baud ===\n")

s = serial.Serial(PORT, BAUD, timeout=2)
time.sleep(0.3)
req_id = 1

# Test gpio.read on common ESP32-S3 pins
print("--- gpio.read ---")
for pin in [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 38, 39, 40, 41, 42, 45, 46, 47, 48]:
    r = send_rpc(s, "gpio.read", {"pin": pin}, req_id)
    req_id += 1
    if r and 'result' in r:
        res = r['result']
        if res.get('ok'):
            print(f"  pin {pin:2d}: value={res.get('value')}  mode={res.get('mode', '?')}")
        elif 'error' not in str(res):
            print(f"  pin {pin:2d}: {res}")

# Test gpio.write - set pin 2 HIGH then LOW as a safe blink test
print("\n--- gpio.write pin 2 (common LED pin) ---")
for val in [1, 0, 1, 0]:
    r = send_rpc(s, "gpio.write", {"pin": 2, "value": val}, req_id)
    req_id += 1
    print(f"  write pin=2 value={val} => {r}")
    time.sleep(0.3)

# Try more method namespaces
print("\n--- Discovering more namespaces ---")
extra_methods = [
    ("gpio.list", None),
    ("gpio.config", None),
    ("gpio.mode", {"pin": 2}),
    ("gpio.set_mode", {"pin": 2, "mode": "output"}),
    ("gpio.get_mode", {"pin": 2}),
    ("gpio.direction", {"pin": 2}),
    ("pwm.set", {"pin": 2, "duty": 50}),
    ("pwm.get", {"pin": 2}),
    ("i2c.scan", None),
    ("spi.transfer", None),
    ("uart.read", None),
    ("adc.read", {"pin": 1}),
    ("dac.write", {"pin": 17, "value": 128}),
    ("system.info", None),
    ("sys.info", None),
    ("device.info", None),
    ("wifi.status", None),
    ("ble.status", None),
    ("ota.status", None),
    ("nvs.get", {"key": "test"}),
    ("log.level", None),
]
for method, params in extra_methods:
    r = send_rpc(s, method, params, req_id)
    req_id += 1
    if r and 'result' in r:
        print(f"  *** SUCCESS [{method}] => {json.dumps(r['result'])}")
    elif r and 'error' in r and r['error'].get('code') != -32601:
        print(f"  [{method}] error: {r['error']}")

s.close()
print("\nDone.")

