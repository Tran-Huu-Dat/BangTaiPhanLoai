import json
from pathlib import Path
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from fastapi.testclient import TestClient

from app import Dashboard, app
from protocol import Ack, Availability, Telemetry
from serial_bridge import SerialState


class SerialBridgeTests(unittest.TestCase):
    def test_idle_status_builds_valid_telemetry(self):
        st = SerialState(device_id="pico-01", default_h0=82.0)
        avail = Availability.model_validate(st.build_availability("online"))
        self.assertEqual(avail.status, "online")

        line = "TRAM1: d=  80mm [IDLE     | # 0] TRAM2: d=  84mm [IDLE     | # 0]"
        self.assertTrue(st.parse_line(line))
        tel = Telemetry.model_validate(st.build_telemetry())
        self.assertEqual(tel.station_1.distance_mm, 80)
        self.assertEqual(tel.station_1.height_mm, 2.0)
        self.assertTrue(tel.station_1.sample_valid)
        self.assertEqual(tel.station_2.distance_mm, 84)
        self.assertEqual(tel.station_2.height_mm, -2.0)
        self.assertEqual(tel.faults, [])

    def test_detection_calib_and_sensor_fault(self):
        st = SerialState(device_id="pico-01", default_h0=82.0)
        self.assertTrue(st.parse_line("[TRAM1] CALIB C1 (30 mau): H0=83.5mm, sigma0=0.40mm -> DAT"))
        self.assertTrue(st.parse_line("[TRAM2] QUA TRAM: h=4.0mm size=S -> PASS"))
        self.assertTrue(st.parse_line("TRAM1: d=  70mm [ACTIVE   | # 3] TRAM2: d=  55mm [IDLE     | # 2]"))
        tel = Telemetry.model_validate(st.build_telemetry())
        self.assertEqual(tel.station_1.reference_mm, 83.5)
        self.assertEqual(tel.station_1.height_mm, 13.5)
        self.assertEqual(tel.trays.S, 1)
        self.assertEqual(tel.trays.M, 2)
        self.assertEqual(tel.trays.L, 3)

        self.assertTrue(st.parse_line("TRAM1: I2C_TIMEOUT !          TRAM2: d=  82mm [IDLE     | # 2]"))
        tel_err = Telemetry.model_validate(st.build_telemetry())
        self.assertFalse(tel_err.station_1.sample_valid)
        self.assertIn("TOF_UNAVAILABLE_1", tel_err.faults)
        self.assertTrue(tel_err.station_2.sample_valid)

    def test_command_mapping_and_ack(self):
        st = SerialState(device_id="pico-01", default_h0=82.0)
        data, reason = st.command_to_serial("PAUSE_SORTING", None)
        self.assertEqual(data, b"p\n")
        self.assertEqual(reason, "OK")
        self.assertEqual(st.mode, "idle")

        data_t1, reason_t1 = st.command_to_serial("TEST_PUSHER", 1)
        self.assertEqual(data_t1, b"1\n")
        ack = Ack.model_validate({
            "schema_version": 1,
            "request_id": "req-1",
            "link_id": st.link_id,
            "result": "executed",
            "reason": reason_t1,
            "mode": st.mode,
        })
        self.assertEqual(ack.result, "executed")
        self.assertTrue(st.parse_line("[TRAM2] TEST: servo day (2500 us)"))
        self.assertEqual(st.stations[2]["push_count"], 1)

    def test_offline_pico_wifi_scan_and_provision_uf2(self):
        with tempfile.TemporaryDirectory() as tmp:
            dash = Dashboard("pico-01", Path(tmp) / "offline-wifi.sqlite3")
            try:
                with patch.object(app.state, "dashboard", dash, create=True), \
                        patch.object(app.state, "bridge", SimpleNamespace(publish=lambda *_: True), create=True), \
                        patch("app.scan_windows_networks", return_value=[
                            {"ssid": "Pegatron-2.4G", "rssi": -48, "secure": True},
                        ]), \
                        patch("app._detect_lan_ipv4", return_value="192.168.1.55"), \
                        patch("app._provision_local_uf2", return_value=(True, "PROVISIONED_UF2:192.168.1.55")) as prov:
                    http = TestClient(app, base_url="http://127.0.0.1")
                    init_wifi = http.get("/api/wifi").json()
                    self.assertEqual(init_wifi["source"], "windows_provision")

                    scan_res = http.post("/api/wifi/scan")
                    self.assertEqual(scan_res.status_code, 202)
                    req_scan = scan_res.json()["request_id"]
                    for _ in range(40):
                        st = http.get("/api/wifi").json()
                        if st["scan"] and st["scan"]["request_id"] == req_scan:
                            break
                        time.sleep(0.05)
                    self.assertTrue(st["scan"]["ok"])
                    self.assertEqual(st["scan"]["networks"][0]["ssid"], "Pegatron-2.4G")

                    conn_res = http.post("/api/wifi/connect", json={"ssid": "Pegatron-2.4G", "password": "secret-pass"})
                    self.assertEqual(conn_res.status_code, 202)
                    req_conn = conn_res.json()["request_id"]
                    for _ in range(40):
                        st = http.get("/api/wifi").json()
                        if st["connect"] and st["connect"]["request_id"] == req_conn:
                            break
                        time.sleep(0.05)
                    prov.assert_called_once_with("Pegatron-2.4G", "secret-pass", "192.168.1.55")
                    self.assertTrue(st["connect"]["ok"])
                    self.assertEqual(st["connect"]["reason"], "PROVISIONED_UF2:192.168.1.55")
                    self.assertNotIn("secret-pass", json.dumps(st))
            finally:
                dash.store.db.close()

    def test_connect_host_wifi_without_password_leak(self):
        with tempfile.TemporaryDirectory() as tmp:
            dash = Dashboard("pico-01", Path(tmp) / "host-wifi.sqlite3")
            try:
                with patch.object(app.state, "dashboard", dash, create=True), \
                        patch.object(app.state, "bridge", SimpleNamespace(publish=lambda *_: True), create=True), \
                        patch("app.get_current_windows_ssid", return_value="BM KTMT"), \
                        patch("app.get_current_windows_wifi", return_value=("BM KTMT", "host-secret-pw")), \
                        patch("app._detect_lan_ipv4", return_value="192.168.1.55"), \
                        patch("app._provision_local_uf2", return_value=(True, "PROVISIONED_UF2:192.168.1.55")) as prov:
                    http = TestClient(app, base_url="http://127.0.0.1")
                    wifi_info = http.get("/api/wifi").json()
                    self.assertEqual(wifi_info["host_ssid"], "BM KTMT")

                    res = http.post("/api/wifi/connect-host")
                    self.assertEqual(res.status_code, 202)
                    req_id = res.json()["request_id"]
                    for _ in range(40):
                        st = http.get("/api/wifi").json()
                        if st["connect"] and st["connect"]["request_id"] == req_id:
                            break
                        time.sleep(0.05)
                    prov.assert_called_once_with("BM KTMT", "host-secret-pw", "192.168.1.55")
                    self.assertTrue(st["connect"]["ok"])
                    self.assertEqual(st["connect"]["reason"], "PROVISIONED_UF2:192.168.1.55")
                    self.assertNotIn("host-secret-pw", json.dumps(st))
            finally:
                dash.store.db.close()

    def test_station_config_api_and_serial_format(self):
        from serial_bridge import format_config_command

        cmd = format_config_command({
            "l_min_mm": 24,
            "l_max_mm": 90.5,
            "m_min_mm": 8,
            "m_max_mm": 18,
            "station_1_on_ms": 250,
            "station_1_delay_ms": 0,
            "station_2_on_ms": 300,
            "station_2_delay_ms": 0,
            "min_off_ms": 120,
        })
        self.assertEqual(cmd, b"k 24.0 90.5 8.0 18.0 250 0 300 0 120\n")
        self.assertIsNone(format_config_command({
            "l_min_mm": 24, "l_max_mm": 90.5, "m_min_mm": 8, "m_max_mm": 18,
            "station_1_on_ms": 250, "station_1_delay_ms": 15,
            "station_2_on_ms": 300, "station_2_delay_ms": 0, "min_off_ms": 120,
        }))
        self.assertIsNone(format_config_command({
            "l_min_mm": 30,
            "l_max_mm": 25,
            "m_min_mm": 7,
            "m_max_mm": 20,
            "station_1_on_ms": 220,
            "station_1_delay_ms": 0,
            "station_2_on_ms": 220,
            "station_2_delay_ms": 0,
            "min_off_ms": 120,
        }))

        with tempfile.TemporaryDirectory() as tmp:
            db_path = Path(tmp) / "cfg.sqlite3"
            dash = Dashboard("pico-01", db_path)
            published = []
            try:
                def fake_pub(payload, leaf="command", retain=False):
                    published.append((leaf, retain, json.loads(payload)))
                    return True

                with patch.object(app.state, "dashboard", dash, create=True), \
                        patch.object(app.state, "bridge", SimpleNamespace(publish=fake_pub), create=True):
                    http = TestClient(app, base_url="http://127.0.0.1")
                    default_cfg = http.get("/api/config").json()
                    self.assertEqual(default_cfg["l_min_mm"], 22.0)
                    self.assertEqual(default_cfg["m_max_mm"], 20.0)

                    bad = http.post("/api/config", json={**default_cfg, "l_min_mm": 50, "l_max_mm": 40})
                    self.assertEqual(bad.status_code, 422)

                    # Guard band (M=[8, 18], L=[24, 95]) must succeed without coupling errors.
                    new_cfg = {**default_cfg, "m_min_mm": 8, "m_max_mm": 18, "l_min_mm": 24, "l_max_mm": 95, "station_1_on_ms": 300}
                    res = http.post("/api/config", json=new_cfg)
                    self.assertEqual(res.status_code, 200)
                    body = res.json()
                    self.assertTrue(body["ok"])
                    self.assertEqual(body["config"]["m_max_mm"], 18.0)
                    self.assertEqual(body["config"]["l_min_mm"], 24.0)
                    self.assertEqual(published[-1][0], "config/set")
                    self.assertTrue(published[-1][1])
            finally:
                dash.store.db.close()

            reloaded = Dashboard("pico-01", db_path)
            try:
                self.assertEqual(reloaded.get_config()["m_max_mm"], 18.0)
                self.assertEqual(reloaded.get_config()["l_min_mm"], 24.0)
                self.assertEqual(reloaded.get_config()["station_1_on_ms"], 300)
            finally:
                reloaded.store.db.close()


if __name__ == "__main__":
    unittest.main()
