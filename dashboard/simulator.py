"""MQTT device simulator. This module has no hardware I/O."""
import argparse
import os
import queue
import time
from itertools import cycle
from threading import Thread
from uuid import uuid4

import paho.mqtt.client as mqtt
from pydantic import ValidationError

from protocol import Ack, Availability, Command, Event, TRAYS, topic_prefix

SCENARIOS = ("normal", "sss", "mmm", "lll", "missing-tray", "home-fault",
             "heartbeat-loss", "slow-ack", "lost-ack", "maintenance", "noisy-calibration")


class SimulatedSorter:
    def __init__(self, scenario="normal", now=None):
        self.scenario = scenario
        self.started = time.monotonic() if now is None else now
        self.boot_id = uuid4().hex
        self.link_id = ""
        self.seq = 0
        self.epoch = 0
        self.mode = "maintenance" if scenario in ("maintenance", "noisy-calibration", "home-fault") else "idle"
        self.armed = True
        self.estop = False
        self.faults = []
        self.home = [True, True]
        self.heights = [0, 0]
        self.pushes = [0, 0]
        self.references = [80.0, 80.0]
        self.counts = dict.fromkeys(TRAYS, 0)
        if scenario == "missing-tray":
            self.counts["L"] = None
        self.sequence = cycle({"sss": "S", "mmm": "M", "lll": "L"}.get(scenario, "SML"))
        self.next_product = self.started
        self.next_sample = self.started
        self.heartbeat = True
        self.dirty = True
        self.operation = None
        self.results = {}
        self.outbox = []

    def uptime(self, now):
        return max(0, int((now - self.started) * 1000))

    def start_link(self, link_id):
        self.link_id = link_id
        self.dirty = True

    def lose_link(self):
        self.link_id = ""
        self.armed = False
        self.epoch += 1
        self.cancel("LINK_LOST")
        self.results.clear()
        self.outbox.clear()

    def event(self, kind, message, **details):
        if self.link_id:
            self.outbox.append(("events", Event(schema_version=1, boot_id=self.boot_id, event_id=uuid4().hex,
                                               kind=kind, message=message, **details).model_dump_json(), 0))
        self.dirty = True

    def change_mode(self, mode):
        if self.mode != mode:
            self.mode = mode
            self.epoch += 1
            self.event("mode_changed", mode)

    def local(self, key):
        if key == "h":
            self.heartbeat = not self.heartbeat
            self.dirty = True
            if not self.heartbeat:
                self.armed = False
                self.epoch += 1
                self.cancel("LOCAL_OVERRIDE")
            return
        if key == "e":
            self.estop = True
            self.armed = False
            self.epoch += 1
            self.change_mode("fault")
            self.cancel("ESTOP")
            self.event("fault_raised", "ESTOP")
            return
        if key == "r":
            for code in self.faults + (["ESTOP"] if self.estop else []):
                self.event("fault_cleared", code)
            self.estop, self.faults, self.home = False, [], [True, True]
            self.armed = False
            self.epoch += 1
            self.change_mode("idle")
            return
        if self.estop or self.faults:
            return
        if key == "m" and self.mode in ("idle", "maintenance"):
            self.cancel("LOCAL_OVERRIDE")
            self.change_mode("maintenance" if self.mode == "idle" else "idle")
            self.armed = True
        elif key == "a" and self.mode in ("idle", "maintenance"):
            self.armed = True
            self.epoch += 1
            self.dirty = True
        elif key == "p":
            self.armed = False
            self.epoch += 1
            self.change_mode("idle")
            self.cancel("LOCAL_OVERRIDE")
            self.dirty = True

    def reason(self, action, station=None):
        if self.estop:
            return "ESTOP"
        if self.faults:
            return "FAULT_LOCKED"
        if action == "PAUSE_SORTING":
            return "OK" if self.mode == "sorting" or self.operation else "ALREADY_PAUSED"
        if not self.armed:
            return "LOCAL_ARM_REQUIRED"
        if self.operation:
            return "COMMAND_PENDING"
        if action == "ENABLE_SORTING":
            return "OK" if self.mode == "idle" and all(self.home) else "IDLE_AND_HOME_REQUIRED"
        if self.mode != "maintenance":
            return "MAINTENANCE_REQUIRED"
        if not self.home[station - 1]:
            return "HOME_REQUIRED"
        return "OK"

    def telemetry(self, now):
        def permission(action, station=None):
            reason = self.reason(action, station)
            return {"allowed": reason == "OK", "reason": reason}

        self.seq += 1
        self.dirty = False
        return {
            "schema_version": 1, "boot_id": self.boot_id, "link_id": self.link_id,
            "seq": self.seq, "uptime_ms": self.uptime(now), "control_epoch": self.epoch,
            "mode": self.mode, "estop": self.estop, "faults": self.faults[:],
            "trays": self.counts.copy(),
            "permissions": {
                "ENABLE_SORTING": permission("ENABLE_SORTING"),
                "PAUSE_SORTING": permission("PAUSE_SORTING"),
                "TEST_PUSHER": {f"station_{i}": permission("TEST_PUSHER", i) for i in (1, 2)},
                "CALIBRATE_TOF": {f"station_{i}": permission("CALIBRATE_TOF", i) for i in (1, 2)},
            },
            **{f"station_{i + 1}": {"height_mm": self.heights[i], "sample_valid": True,
                                  "home": self.home[i], "push_count": self.pushes[i],
                                  "reference_mm": self.references[i]} for i in (0, 1)},
        }

    def answer(self, command, reason="OK", now=None):
        now = time.monotonic() if now is None else now
        ack = Ack(schema_version=1, request_id=command.request_id, link_id=command.link_id,
                  result="executed" if reason == "OK" else "rejected", reason=reason, mode=self.mode)
        payload = ack.model_dump_json()
        self.results[command.request_id] = (self.uptime(now) + 10000, payload)
        if self.scenario != "lost-ack":
            self.outbox.append(("ack", payload, now + (6 if self.scenario == "slow-ack" else 0)))

    def cancel(self, reason, now=None):
        if self.operation:
            command, _ = self.operation
            self.operation = None
            if command.action == "TEST_PUSHER":
                self.home[command.station - 1] = True
            self.answer(command, reason, now)

    def command(self, command, now):
        if command.link_id != self.link_id:
            return
        if command.request_id in self.results:
            if self.scenario not in ("lost-ack", "slow-ack"):
                self.outbox.append(("ack", self.results[command.request_id][1], now))
            return
        if self.operation and command.request_id == self.operation[0].request_id:
            return
        age = self.uptime(now) - command.observed_uptime_ms
        if not 0 <= age < 5000:
            self.answer(command, "EXPIRED", now)
            return
        if command.control_epoch != self.epoch or not self.heartbeat:
            self.answer(command, "LOCAL_OVERRIDE", now)
            return
        reason = self.reason(command.action, command.station)
        if reason != "OK":
            self.answer(command, reason, now)
            return

        if command.action == "ENABLE_SORTING":
            self.change_mode("sorting")
            self.next_product = now + 0.2
            self.answer(command, now=now)
        elif command.action == "PAUSE_SORTING":
            self.armed = False
            self.change_mode("idle")
            self.cancel("PAUSED", now)
            self.answer(command, now=now)
        else:
            index = command.station - 1
            if command.action == "TEST_PUSHER":
                self.pushes[index] += 1
                self.home[index] = False
            self.operation = (command, now + (0.5 if command.action == "CALIBRATE_TOF" else 0.18))
            self.dirty = True

    def tick(self, now):
        self.results = {key: entry for key, entry in self.results.items() if entry[0] > self.uptime(now)}
        if self.mode == "sorting" and now >= self.next_product:
            self.next_product = now + 0.2
            tray = next(self.sequence)
            self.heights = [dict(S=30, M=50, L=70)[tray], dict(S=0, M=50, L=70)[tray]]
            if self.counts[tray] is not None:
                self.counts[tray] += 1
            if tray in ("S", "M"):
                self.pushes[int(tray == "M")] += 1

        if self.operation and now >= self.operation[1]:
            command, _ = self.operation
            self.operation = None
            index = command.station - 1
            reason = "OK"
            if command.action == "TEST_PUSHER":
                if self.scenario == "home-fault":
                    reason = f"HOME_TIMEOUT_{command.station}"
                    self.faults.append(reason)
                    self.armed = False
                    self.change_mode("fault")
                    self.event("fault_raised", reason, station=command.station)
                else:
                    self.home[index] = True
            else:
                samples = [80 + command.station * 0.4 + ((i % 3) - 1) * 0.2 for i in range(50)]
                if self.scenario == "noisy-calibration":
                    samples[0] += 4
                reference, reason = calibration(samples, 80.0)
                if reason == "OK":
                    self.references[index] = reference
                    self.event("calibrated", "CALIBRATED", station=command.station, reference_mm=reference)
            self.answer(command, reason, now)
            self.dirty = True

    def drain(self, now):
        ready = [entry for entry in self.outbox if entry[2] <= now]
        self.outbox = [entry for entry in self.outbox if entry[2] > now]
        return ready


def calibration(samples, original):
    if len(samples) < 50:
        return original, "TOO_FEW_SAMPLES"
    values = sorted(samples[:50])
    if values[-1] - values[0] > 3:
        return original, "UNSTABLE_SURFACE"
    reference = round((values[24] + values[25]) / 2, 2)
    if abs(reference - original) > 5:
        return original, "REFERENCE_OUT_OF_RANGE"
    return reference, "OK"


SIM_NETWORKS = (("Pegatron-Lab", -48, True), ("Xuong-San-Xuat", -63, True), ("Khach", -71, False))


def simulated_wifi(payload):
    """Fake scan/connect answers. Password 'wrong-pass' fails like a bad key would."""
    import json

    try:
        request = json.loads(payload)
        kind, request_id = request["action"], request["request_id"]
    except (ValueError, KeyError, TypeError):
        return "{}"
    ok, reason, ssid = True, "OK", None
    if kind == "connect":
        ssid = request.get("ssid")
        if request.get("password") == "wrong-pass":
            ok, reason = False, "WIFI_FAILED"
    networks = [{"ssid": n, "rssi": r, "secure": s} for n, r, s in SIM_NETWORKS] if kind == "scan" else []
    return json.dumps({"schema_version": 1, "request_id": request_id, "kind": kind, "ok": ok,
                       "reason": reason, "ssid": ssid, "networks": networks})


def run(scenario, host, port, keys):
    import json

    prefix = topic_prefix("sorter-sim")
    sim = SimulatedSorter(scenario)
    heartbeat_failure = time.monotonic() + 12 if scenario == "heartbeat-loss" else float("inf")
    while True:
        link_id = uuid4().hex
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="pegatron-simulator",
                             clean_session=True, protocol=mqtt.MQTTv311, reconnect_on_failure=False)
        client.connect_timeout = 2.0
        if username := os.getenv("PEGATRON_SIM_USER"):
            client.username_pw_set(username, os.getenv("PEGATRON_SIM_PASSWORD"))

        def availability(status):
            return Availability(schema_version=1, status=status, link_id=link_id).model_dump_json()

        client.will_set(prefix + "/availability", availability("offline"), qos=1, retain=True)

        def on_connect(client, userdata, flags, reason, properties):
            if reason.is_failure:
                client.disconnect()
            else:
                client.subscribe([(prefix + "/command", 1), (prefix + "/wifi/request", 1)])

        def on_subscribe(client, userdata, mid, reasons, properties):
            if any(reason.is_failure for reason in reasons):
                client.disconnect()
                return
            sim.start_link(link_id)
            client.publish(prefix + "/availability", availability("online"), qos=1, retain=True)

        def on_message(client, userdata, message):
            if message.topic == prefix + "/wifi/request" and not message.retain and len(message.payload) <= 1024:
                client.publish(prefix + "/wifi/result", simulated_wifi(message.payload), qos=1)
                return
            if message.topic == prefix + "/command" and not message.retain and len(message.payload) <= 1024:
                try:
                    sim.command(Command.model_validate_json(message.payload), time.monotonic())
                except ValidationError:
                    pass

        client.on_connect, client.on_subscribe, client.on_message = on_connect, on_subscribe, on_message
        try:
            client.connect(host, port, keepalive=10)
            while client.loop(timeout=0.05) == mqtt.MQTT_ERR_SUCCESS:
                while not keys.empty():
                    sim.local(keys.get_nowait())
                now = time.monotonic()
                if now >= heartbeat_failure:
                    sim.local("h")
                    heartbeat_failure = float("inf")
                sim.tick(now)
                if sim.link_id and sim.heartbeat and (sim.dirty or now >= sim.next_sample):
                    client.publish(prefix + "/telemetry", json.dumps(sim.telemetry(now)), qos=0)
                    sim.next_sample = now + 1
                for leaf, payload, _ in sim.drain(now):
                    client.publish(prefix + "/" + leaf, payload, qos=1)
        except OSError as error:
            print(f"MQTT: {error}. Reconnecting.", flush=True)
        except KeyboardInterrupt:
            if sim.link_id:
                info = client.publish(prefix + "/availability", availability("offline"), qos=1, retain=True)
                deadline = time.monotonic() + 0.5
                while info.rc == mqtt.MQTT_ERR_SUCCESS and not info.is_published() and time.monotonic() < deadline:
                    client.loop(timeout=0.05)
            client.disconnect()
            return
        sim.lose_link()
        client.disconnect()
        time.sleep(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=SCENARIOS, default="normal")
    parser.add_argument("--host", default=os.getenv("PEGATRON_MQTT_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.getenv("PEGATRON_MQTT_PORT", "1883")))
    parser.add_argument("--interactive", action="store_true")
    args = parser.parse_args()
    keys = queue.SimpleQueue()
    if args.interactive:
        print("a: arm | m: maintenance | p: local pause | e: E-STOP | r: reset | h: heartbeat (press Enter)")

        def read_keys():
            try:
                while True:
                    keys.put(input().strip())
            except EOFError:
                return

        Thread(target=read_keys, daemon=True).start()
    try:
        run(args.scenario, args.host, args.port, keys)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

