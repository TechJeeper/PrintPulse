#!/usr/bin/env python3
"""PrintPulse LAN bridge for Bambu Lab printers.

Bambu printers only talk MQTT over TLS on the local network, which the Pebble
phone app can't open. This small server keeps an MQTT connection to each
printer and serves their latest state as plain JSON for PrintPulse:

    GET /status   ->  {"printers": [{"id", "name", "online", "updated", "error", "report": {...}}]}
    POST /command ->  {"id": "<serial>", "command": "pause"|"resume"|"stop"}

Setup (any always-on computer on the same network: Raspberry Pi, NAS, PC):

    pip install paho-mqtt
    python bambu_bridge.py --printer "X1C,192.168.1.50,01S00C000000000,12345678"

--printer takes NAME,IP,SERIAL,ACCESS_CODE and can be repeated. The serial and
LAN access code are on the printer's screen (Settings > Network / Device).
Or list printers in a JSON file and pass --config printers.json:

    [{"name": "X1C", "host": "192.168.1.50", "serial": "01S00C...", "access_code": "12345678"}]

Then in PrintPulse settings add Bambu Lab > LAN bridge > http://<this computer>:8787
"""

import argparse
import json
import ssl
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

try:
    import paho.mqtt.client as mqtt
except ImportError:
    sys.exit("paho-mqtt is missing: run `pip install paho-mqtt` first.")

MQTT_PORT = 8883
PUSHALL_EVERY = 300  # P1/A1 only send changes, so ask for a full report now and then
SILENT_AFTER = 2 * PUSHALL_EVERY + 60
KEEP = (
    "gcode_state", "mc_percent", "mc_remaining_time", "layer_num", "total_layer_num",
    "nozzle_temper", "nozzle_target_temper", "bed_temper", "bed_target_temper",
    "chamber_temper", "subtask_name", "gcode_file", "print_error", "stg_cur", "spd_lvl",
)
PUSHALL = json.dumps({"pushing": {"sequence_id": "0", "command": "pushall"}})


def deep_merge(target, delta):
    for key, value in delta.items():
        if isinstance(value, dict) and isinstance(target.get(key), dict):
            deep_merge(target[key], value)
        else:
            target[key] = value


def reason_value(reason):
    return getattr(reason, "value", reason)


class Printer:
    def __init__(self, name, host, serial, access_code):
        self.name = name
        self.host = host
        self.serial = serial
        self.access_code = access_code
        self.report = {}
        self.updated = 0
        self.connected = False
        self.error = "Connecting"
        self.lock = threading.Lock()
        self.client = None

    def start(self):
        client_id = "printpulse-" + self.serial[-6:]
        try:
            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
        except AttributeError:  # paho-mqtt 1.x
            client = mqtt.Client(client_id=client_id)
        client.username_pw_set("bblp", self.access_code)
        tls = ssl.create_default_context()
        tls.check_hostname = False
        tls.verify_mode = ssl.CERT_NONE  # printers use a self-signed certificate
        client.tls_set_context(tls)
        client.on_connect = self._on_connect
        client.on_disconnect = self._on_disconnect
        client.on_message = self._on_message
        client.reconnect_delay_set(min_delay=2, max_delay=60)
        client.connect_async(self.host, MQTT_PORT, keepalive=60)
        client.loop_start()
        self.client = client

    def request_full_report(self):
        if self.client and self.connected:
            self.client.publish("device/%s/request" % self.serial, PUSHALL)

    def send_command(self, command):
        if command not in ("pause", "resume", "stop"):
            return False, "unknown command"
        if not self.client or not self.connected:
            return False, "printer offline"
        payload = json.dumps({"print": {"sequence_id": "0", "command": command, "param": ""}})
        info = self.client.publish("device/%s/request" % self.serial, payload, qos=1)
        if getattr(info, "rc", 0) != 0:
            return False, "publish failed"
        return True, ""

    # paho 1.x passes (client, userdata, flags, rc); 2.x adds properties.
    def _on_connect(self, client, userdata, flags, reason, *rest):
        code = reason_value(reason)
        if code != 0:
            self.connected = False
            self.error = "Wrong access code" if code in (4, 5, 134, 135) else "MQTT error %s" % code
            return
        self.connected = True
        self.error = ""
        client.subscribe("device/%s/report" % self.serial)
        self.request_full_report()

    def _on_disconnect(self, client, userdata, *rest):
        self.connected = False
        self.error = "Printer unreachable"

    def _on_message(self, client, userdata, msg):
        try:
            data = json.loads(msg.payload)
        except ValueError:
            return
        delta = data.get("print") if isinstance(data, dict) else None
        if not isinstance(delta, dict):
            return
        with self.lock:
            deep_merge(self.report, delta)
            self.updated = int(time.time())

    def snapshot(self):
        with self.lock:
            report = {k: self.report[k] for k in KEEP if k in self.report}
            updated = self.updated
        fresh = updated and time.time() - updated < SILENT_AFTER
        online = bool(self.connected and fresh)
        error = self.error or ("" if online else "No reports from printer")
        return {
            "id": self.serial,
            "name": self.name,
            "online": online,
            "updated": updated,
            "error": error,
            "report": report,
        }


class Handler(BaseHTTPRequestHandler):
    printers = []
    token = ""

    def _authorized(self, url):
        given = self.headers.get("X-Bridge-Token") or parse_qs(url.query).get("token", [""])[0]
        return not self.token or given == self.token

    def do_GET(self):
        url = urlparse(self.path)
        if url.path not in ("/", "/status"):
            return self._send(404, {"error": "not found"})
        if not self._authorized(url):
            return self._send(401, {"error": "bad token"})
        self._send(200, {"printers": [p.snapshot() for p in self.printers]})

    def do_POST(self):
        url = urlparse(self.path)
        if url.path != "/command":
            return self._send(404, {"error": "not found"})
        if not self._authorized(url):
            return self._send(401, {"error": "bad token"})
        length = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
        except ValueError:
            return self._send(400, {"error": "bad json"})
        serial = str(body.get("id") or "")
        command = str(body.get("command") or "")
        printer = next((p for p in self.printers if p.serial == serial), None)
        if not printer:
            return self._send(404, {"error": "printer not found"})
        ok, err = printer.send_command(command)
        if not ok:
            return self._send(409, {"error": err})
        self._send(200, {"ok": True})

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, X-Bridge-Token")
        self.end_headers()

    def _send(self, code, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass


def parse_printer(spec):
    parts = [p.strip() for p in spec.split(",")]
    if len(parts) != 4 or not all(parts):
        raise argparse.ArgumentTypeError("use NAME,IP,SERIAL,ACCESS_CODE")
    return Printer(*parts)


def load_config(path):
    with open(path, encoding="utf-8") as f:
        entries = json.load(f)
    return [Printer(e["name"], e["host"], e["serial"], e["access_code"]) for e in entries]


def main():
    parser = argparse.ArgumentParser(description="Serve Bambu Lab printer status for PrintPulse.")
    parser.add_argument("--printer", action="append", type=parse_printer, default=[],
                        metavar="NAME,IP,SERIAL,CODE", help="a printer to watch (repeatable)")
    parser.add_argument("--config", help="JSON file listing printers")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--bind", default="0.0.0.0", help="address to listen on")
    parser.add_argument("--token", default="", help="require this token from PrintPulse")
    args = parser.parse_args()

    printers = list(args.printer)
    if args.config:
        printers += load_config(args.config)
    if not printers:
        parser.error("add at least one --printer or a --config file")

    for p in printers:
        p.start()

    def refresh_loop():
        while True:
            time.sleep(PUSHALL_EVERY)
            for p in printers:
                p.request_full_report()

    threading.Thread(target=refresh_loop, daemon=True).start()

    Handler.printers = printers
    Handler.token = args.token
    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    names = ", ".join(p.name for p in printers)
    print("PrintPulse bridge for %s on http://%s:%d/status" % (names, args.bind, args.port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
