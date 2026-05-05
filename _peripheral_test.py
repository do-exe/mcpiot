"""
ADC + I2C hardware validation test
────────────────────────────────────
Phase 1 — Raw JSON      : sends/receives literal JSON-RPC strings
Phase 2 — mcpiot_repl.py: spawns the generated REPL and pipes commands

Tests:
  adc.read  on GPIO 1  (ADC1_CH0 — even floating returns a valid raw value)
  i2c.scan  on SDA=8 SCL=9  (returns whatever devices are connected, or [])
"""
import serial, serial.tools.list_ports, json, time, sys, subprocess, re, os

ANSI = re.compile(r'\x1b\[[0-9;]*m|\x1b\[[\d;]*[A-Za-z]')

ADC_PIN   = 1    # ADC1_CH0, safe ADC1 pin
I2C_SDA   = 8    # free GPIO on ESP32-S3-DevKit
I2C_SCL   = 9    # free GPIO on ESP32-S3-DevKit

PASS = "\033[92mPASS\033[0m"
FAIL = "\033[91mFAIL\033[0m"

def strip_ansi(text):
    return ANSI.sub('', text)

def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    sys.exit("ESP32-S3 not found on any COM port")

# ══════════════════════════════════════════════════════════════════════
# Phase 1 — Raw JSON
# ══════════════════════════════════════════════════════════════════════
print("\n" + "═"*62)
print("  PHASE 1  —  Raw JSON  (AI / agent interface)")
print("═"*62)

port = find_port()
print(f"\n  Port: {port}\n")
s = serial.Serial(port, 115200, timeout=1)
time.sleep(0.5)
_id = 1

def rpc(method, params=None, wait=8):
    global _id
    req_id = _id
    obj = {"jsonrpc": "2.0", "method": method, "id": req_id}
    if params is not None:
        obj["params"] = params
    _id += 1
    raw = json.dumps(obj, ensure_ascii=False)
    print(f"  -> {raw}")
    s.reset_input_buffer()
    s.write((raw + "\n").encode())
    deadline = time.time() + wait
    buf = b""
    while time.time() < deadline:
        chunk = s.read(s.in_waiting or 1)
        if chunk:
            buf += chunk
            if b"\n" in buf:
                for line in buf.decode("utf-8", "replace").splitlines():
                    line = line.strip()
                    if line.startswith("{"):
                        try:
                            resp = json.loads(line)
                            if resp.get("id") == req_id:
                                print(f"  <- {line}")
                                return resp
                        except Exception:
                            pass
                buf = b""
        time.sleep(0.05)
    print("  <- (timeout)")
    return None

p1_pass = 0
p1_fail = 0

def check(name, resp, validator):
    global p1_pass, p1_fail
    if resp is None:
        print(f"  [{FAIL}] {name}: no response")
        p1_fail += 1
        return
    if "error" in resp:
        print(f"  [{FAIL}] {name}: error -> {resp['error']}")
        p1_fail += 1
        return
    result = resp.get("result")
    ok, msg = validator(result)
    if ok:
        print(f"  [{PASS}] {name}: {msg}")
        p1_pass += 1
    else:
        print(f"  [{FAIL}] {name}: {msg}")
        p1_fail += 1

# ── adc.read ──────────────────────────────────────────────────────────
print(f"\n[adc.read pin={ADC_PIN}]")
resp = rpc("adc.read", {"pin": ADC_PIN})

def validate_adc(result):
    if not isinstance(result, dict):
        return False, f"result is not a dict: {result}"
    if "raw" not in result:
        return False, f"missing 'raw' field: {result}"
    raw = result["raw"]
    mv  = result.get("mv", -1)
    if not (0 <= raw <= 4095):
        return False, f"raw={raw} out of range 0-4095"
    return True, f"pin={result.get('pin')} ch={result.get('channel')} raw={raw} mv={mv}"

check("adc.read", resp, validate_adc)

# ── i2c.scan ──────────────────────────────────────────────────────────
print(f"\n[i2c.scan sda={I2C_SDA} scl={I2C_SCL}]")
resp = rpc("i2c.scan", {"sda": I2C_SDA, "scl": I2C_SCL}, wait=8)

def validate_i2c_scan(result):
    if not isinstance(result, list):
        return False, f"result is not a list: {result}"
    if len(result) == 0:
        return True, "scan returned [] (no I2C devices connected — that is valid)"
    addrs = [f"{d.get('hex','?')}" for d in result if isinstance(d, dict)]
    return True, f"found {len(result)} device(s): {', '.join(addrs)}"

check("i2c.scan", resp, validate_i2c_scan)

# ── adc.read with wrong pin (expect error) ────────────────────────────
print(f"\n[adc.read pin=19 - ADC2, should return error]")
resp = rpc("adc.read", {"pin": 19})

def validate_adc_error(result):
    # adc.read on GPIO 19 is ADC2 — driver must refuse it
    return False, "expected an error response but got result"

if resp and "error" in resp:
    print(f"  [{PASS}] adc.read pin=19 (ADC2): correctly returned error -> {resp['error']['message']}")
    p1_pass += 1
elif resp is None:
    print(f"  [{FAIL}] adc.read pin=19 (ADC2): no response")
    p1_fail += 1
else:
    check("adc.read pin=19 (ADC2 should error)", resp, validate_adc_error)

s.close()

print(f"\n  Phase 1 summary: {p1_pass} passed, {p1_fail} failed\n")

# ══════════════════════════════════════════════════════════════════════
# Phase 2 — mcpiot_repl.py (human shorthand interface)
# ══════════════════════════════════════════════════════════════════════
print("═"*62)
print("  PHASE 2  —  REPL (human shorthand interface)")
print("═"*62 + "\n")

cmds = "\n".join([
    f"adc.read {ADC_PIN}",
    f"i2c.scan {I2C_SDA} {I2C_SCL}",
    "quit",
])

result = subprocess.run(
    [sys.executable, "mcpiot_repl.py"],
    input=cmds,
    capture_output=True,
    text=True,
    encoding="utf-8",
    errors="replace",
    timeout=30,
    env={**os.environ, "PYTHONIOENCODING": "utf-8", "PYTHONUTF8": "1"},
)

output = strip_ansi(result.stdout + result.stderr)
print(output)

p2_pass = 0
p2_fail = 0

# adc.read must produce raw field
if '"raw"' in output or "'raw'" in output or "raw" in output:
    print(f"[{PASS}] Phase 2 adc.read: 'raw' seen in output")
    p2_pass += 1
else:
    print(f"[{FAIL}] Phase 2 adc.read: 'raw' NOT seen in output")
    p2_fail += 1

# i2c.scan must produce a list (empty is fine)
if '[]' in output or '"addr"' in output or "addr" in output or "found" in output.lower() or "scan returned" in output.lower():
    print(f"[{PASS}] Phase 2 i2c.scan: list result seen in output")
    p2_pass += 1
else:
    print(f"[{FAIL}] Phase 2 i2c.scan: no list result in output")
    p2_fail += 1

print(f"\nPhase 2 summary: {p2_pass} passed, {p2_fail} failed")

total_pass = p1_pass + p2_pass
total_fail = p1_fail + p2_fail
print(f"\n{'='*62}")
print(f"  TOTAL: {total_pass} passed, {total_fail} failed")
if total_fail == 0:
    print("  ALL TESTS PASSED")
else:
    print("  SOME TESTS FAILED")
print("="*62 + "\n")
