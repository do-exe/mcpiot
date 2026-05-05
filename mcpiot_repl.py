"""
MCP-IoT interactive REPL
Usage: python mcpiot_repl.py

Shortcuts:
  mcpiot.info
  mcpiot.capabilities
  gpio.read <pin>
  gpio.write <pin> <0|1>
  adc.read <pin>
  pwm.set <pin> <duty 0-100> [freq_hz]
  sensor.read <type>
  Or type raw JSON: {"method":"gpio.read","params":{"pin":2}}

Type 'help' for command list, 'quit' to exit.
"""

import serial
import serial.tools.list_ports
import json
import time
import sys

try:
    import readline  # Unix
except ImportError:
    try:
        import pyreadline3 as readline  # Windows
    except ImportError:
        pass  # no history — still works fine

# ── ANSI colours ────────────────────────────────────────────────────────────
RESET  = "\033[0m"
BOLD   = "\033[1m"
GREEN  = "\033[32m"
YELLOW = "\033[33m"
CYAN   = "\033[36m"
RED    = "\033[31m"
DIM    = "\033[2m"

def c(text, colour):
    return colour + text + RESET

# ── Port auto-detect ─────────────────────────────────────────────────────────
ESP32S3_VID, ESP32S3_PID = 0x303A, 0x1001

def find_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if p.vid == ESP32S3_VID and p.pid == ESP32S3_PID:
            return p.device, p.description
    for p in ports:
        if any(kw in (p.description or '').lower() for kw in ('usb serial', 'cp210', 'ch340', 'ftdi')):
            return p.device, p.description
    raise RuntimeError("No ESP32 device found. Is it connected?")

# ── Send / receive ───────────────────────────────────────────────────────────
_req_id = 1

def send(s, method, params=None):
    global _req_id
    req = {"jsonrpc": "2.0", "method": method, "id": _req_id}
    if params:
        req["params"] = params
    _req_id += 1
    s.write((json.dumps(req) + "\n").encode())

    deadline = time.time() + 3
    buf = b""
    while time.time() < deadline:
        chunk = s.read(s.in_waiting or 1)
        if chunk:
            buf += chunk
            if b"\n" in buf:
                break
        time.sleep(0.02)

    for line in buf.decode("utf-8", errors="replace").splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                pass
    return None

# ── Pretty-print response ────────────────────────────────────────────────────
def pretty(resp):
    if resp is None:
        print(c("  ✗ No response (timeout)", RED))
        return
    if "error" in resp:
        err = resp["error"]
        print(c(f"  ✗ Error {err.get('code')}: {err.get('message')}", RED))
    elif "result" in resp:
        result = resp["result"]
        if isinstance(result, dict):
            # Flatten simple dicts to one line each
            lines = json.dumps(result, indent=2).splitlines()
            print(c("  ← ", GREEN) + c(lines[0], CYAN))
            for l in lines[1:]:
                print("     " + c(l, CYAN))
        else:
            print(c(f"  ← {result}", CYAN))

# ── Parse shorthand input ────────────────────────────────────────────────────
def parse_input(text):
    text = text.strip()
    if not text:
        return None, None

    # Raw JSON passthrough
    if text.startswith("{"):
        try:
            obj = json.loads(text)
            return obj.get("method"), obj.get("params")
        except json.JSONDecodeError as e:
            print(c(f"  ✗ JSON parse error: {e}", RED))
            return None, None

    parts = text.split()
    method = parts[0]
    args   = parts[1:]

    # Shorthand param mapping
    param_map = {
        "gpio.read":    lambda a: {"pin": int(a[0])} if a else None,
        "gpio.write":   lambda a: {"pin": int(a[0]), "value": int(a[1])} if len(a) >= 2 else None,
        "adc.read":     lambda a: {"pin": int(a[0])} if a else None,
        "pwm.set":      lambda a: {"pin": int(a[0]), "duty": float(a[1]),
                                   "freq": int(a[2]) if len(a) >= 3 else 1000} if len(a) >= 2 else None,
        "sensor.read":  lambda a: {"type": a[0]} if a else None,
    }

    params = None
    if method in param_map and args:
        try:
            params = param_map[method](args)
        except (ValueError, IndexError) as e:
            print(c(f"  ✗ Bad args: {e}", RED))
            return None, None
    elif args:
        # Generic: try to parse remaining args as key=value or positional JSON
        try:
            params = json.loads(" ".join(args))
        except json.JSONDecodeError:
            print(c(f"  ✗ Unknown args format. Use key=value or JSON object.", RED))
            return None, None

    return method, params

# ── Help ─────────────────────────────────────────────────────────────────────
HELP = f"""
{BOLD}MCP-IoT REPL commands:{RESET}

  {CYAN}mcpiot.info{RESET}                              Device info
  {CYAN}mcpiot.capabilities{RESET}                      Full capability manifest

  {BOLD}GPIO:{RESET}
  {CYAN}gpio.read <pin>{RESET}                          Read GPIO pin       e.g. gpio.read 2
  {CYAN}gpio.write <pin> <0|1>{RESET}                   Write GPIO pin      e.g. gpio.write 2 1

  {BOLD}PWM:{RESET}
  {CYAN}pwm.set <pin> <duty> [freq]{RESET}              Set PWM output
    {DIM}duty{RESET}  0–100 (percent)                         e.g. pwm.set 5 50
    {DIM}freq{RESET}  Hz, optional, default 1000 Hz           e.g. pwm.set 5 75 500
    {DIM}Examples:{RESET}
      pwm.set 5 0          → off
      pwm.set 5 50         → 50% @ 1 kHz (default)
      pwm.set 5 100        → full on
      pwm.set 5 50 50      → 50% @ 50 Hz  (servo range)
      pwm.set 5 75 20000   → 75% @ 20 kHz (motor/buzzer)

  {BOLD}ADC / Sensor:{RESET}
  {CYAN}adc.read <pin>{RESET}                           Read ADC pin
  {CYAN}sensor.read <type>{RESET}                       Read sensor         e.g. sensor.read temperature
    {DIM}types:{RESET} temperature, light, distance

  {DIM}Or type raw JSON:{RESET} {{"method":"gpio.read","params":{{"pin":2}}}}

  {YELLOW}help{RESET}   Show this help
  {YELLOW}quit{RESET}   Exit
"""

# ── Main ─────────────────────────────────────────────────────────────────────
def main():
    try:
        port, desc = find_port()
    except RuntimeError as e:
        print(c(f"✗ {e}", RED))
        sys.exit(1)

    print(c(f"\n  MCP-IoT REPL", BOLD + CYAN))
    print(c(f"  Connected: {port} ({desc})", DIM))
    print(c(  "  Type 'help' for commands, 'quit' to exit\n", DIM))

    try:
        s = serial.Serial(port, 115200, timeout=3)
        time.sleep(0.5)
    except serial.SerialException as e:
        print(c(f"✗ Cannot open {port}: {e}", RED))
        sys.exit(1)

    # Prime with capabilities on connect
    resp = send(s, "mcpiot.capabilities")
    if resp and "result" in resp:
        r = resp["result"]
        print(c(f"  Board: {r.get('board')}  Version: {r.get('version')}  Protocol: {r.get('protocol')}", GREEN))
        for mod in r.get("modules", []):
            methods = ", ".join(mod.get("methods", []))
            print(c(f"    • {mod.get('type')}: {methods}", DIM))
        print()

    while True:
        try:
            text = input(c("mi> ", BOLD + YELLOW))
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not text.strip():
            continue
        if text.strip().lower() in ("quit", "exit", "q"):
            break
        if text.strip().lower() == "help":
            print(HELP)
            continue

        method, params = parse_input(text)
        if method is None:
            continue

        resp = send(s, method, params)
        pretty(resp)

    s.close()
    print(c("  Disconnected.", DIM))

if __name__ == "__main__":
    main()
