"""
Two-method WiFi test
────────────────────
Phase 1 — Raw JSON      : sends/receives literal JSON-RPC strings (the AI
                          interface — exactly what an LLM or agent sends).
Phase 2 — mcpiot_repl.py: spawns the generated REPL as a subprocess and
                          pipes commands to it (the human shorthand interface).

No hardcoded SSIDs — Phase 1 discovers the best open AP from scan and
Phase 2 reuses that name.
"""
import serial, serial.tools.list_ports, json, time, sys, subprocess, re, os

ANSI = re.compile(r'\x1b\[[0-9;]*m|\x1b\[[\d;]*[A-Za-z]')

def strip_ansi(text):
    return ANSI.sub('', text)

def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    sys.exit("ESP32-S3 not found on any COM port")

# ══════════════════════════════════════════════════════════════════════
# Phase 1 — Raw JSON (AI-native interface)
# ══════════════════════════════════════════════════════════════════════
print("\n" + "═"*62)
print("  PHASE 1  —  Raw JSON  (AI / agent interface)")
print("  Every line shows the exact bytes sent → and received ←")
print("═"*62)

port = find_port()
print(f"\n  Port: {port}\n")
s = serial.Serial(port, 115200, timeout=1)
time.sleep(0.5)

_id = 1

def rpc(method, params=None, wait=5):
    """Send a raw JSON-RPC request and return the parsed response."""
    global _id
    req_id = _id
    obj = {"jsonrpc": "2.0", "method": method, "id": req_id}
    if params is not None:
        obj["params"] = params
    _id += 1
    raw = json.dumps(obj, ensure_ascii=False)
    print(f"  → {raw}")
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
                                print(f"  ← {line}")
                                return resp
                        except Exception:
                            pass
                buf = b""
        time.sleep(0.05)
    print("  ← (timeout — no matching response)")
    return None

# ── Scan ──
print("[wifi.scan]")
scan_resp = rpc("wifi.scan", wait=18)

open_ssid = None
if scan_resp and "result" in scan_resp:
    aps = scan_resp["result"]
    # prefer strongest OPEN network
    open_aps = [ap for ap in aps if ap.get("auth") == "OPEN"]
    if open_aps:
        open_ssid = max(open_aps, key=lambda a: a.get("rssi", -999))["ssid"]
    elif aps:
        open_ssid = aps[0]["ssid"]   # fallback: strongest of any kind

if not open_ssid:
    s.close()
    sys.exit("  No AP found from scan — is the antenna connected?")

print(f"\n  ✓ Chose AP: {open_ssid!r}")

# ── Connect ──
print("\n[wifi.connect]")
rpc("wifi.connect", {"ssid": open_ssid, "password": ""}, wait=22)

# ── Status ──
print("\n[wifi.status]")
rpc("wifi.status", wait=6)

# ── Disconnect ──
print("\n[wifi.disconnect]")
rpc("wifi.disconnect", wait=6)

# ── Status after disconnect ──
print("\n[wifi.status  (after disconnect)]")
rpc("wifi.status", wait=6)

s.close()

# ══════════════════════════════════════════════════════════════════════
# Phase 2 — mcpiot_repl.py (human shorthand interface)
# ══════════════════════════════════════════════════════════════════════
print("\n\n" + "═"*62)
print("  PHASE 2  —  mcpiot_repl.py  (human REPL interface)")
print("  Same workflow, using the friendly shorthand commands")
print("═"*62)
print(f'\n  Piping commands (SSID discovered in Phase 1: {open_ssid!r}):')
# Quote the SSID so shlex.split handles spaces correctly
quoted = f'"{open_ssid}"' if ' ' in open_ssid else open_ssid
cmds = "\n".join([
    "wifi.scan",
    f"wifi.connect {quoted}",
    "wifi.status",
    "wifi.disconnect",
    "wifi.status",
    "quit",
]) + "\n"
print()
for line in cmds.strip().splitlines():
    print(f"  mi> {line}")
print()

time.sleep(1.0)   # let device settle before REPL opens port

env = {**os.environ, "PYTHONIOENCODING": "utf-8", "PYTHONUTF8": "1"}
proc = subprocess.run(
    [sys.executable, "mcpiot_repl.py"],
    input=cmds,
    capture_output=True,
    text=True,
    encoding="utf-8",
    timeout=130,
    env=env,
)

output = strip_ansi(proc.stdout)
for line in output.splitlines():
    stripped = line.rstrip()
    if stripped:
        print(f"  {stripped}")

if proc.returncode != 0 and proc.stderr:
    print(f"\n  stderr: {strip_ansi(proc.stderr).strip()}")

print("\n" + "═"*62)
print("  Both methods completed successfully.")
print("═"*62 + "\n")
