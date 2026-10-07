import json
import unittest

import mock_api


class InfoTest(unittest.TestCase):
    def test_info_names_the_firmware_and_its_capabilities(self):
        status, data = mock_api.handle("GET", "/api/info", None)
        self.assertEqual(200, status)
        self.assertEqual("grilly-plus", data["firmware"])
        self.assertEqual(1, data["api_version"])
        self.assertIn("history", data["capabilities"])
        self.assertIn("cook_session", data["capabilities"])
        self.assertIn("alarm_probe_mute", data["capabilities"])
        self.assertIn("power_saving", data["capabilities"])
        self.assertNotIn("ota_auth", data["capabilities"])

    def test_ota_auth_capability_needs_an_admin_password(self):
        previous = mock_api.ADMIN_PASSWORD
        mock_api.ADMIN_PASSWORD = "secret"
        try:
            _, data = mock_api.handle("GET", "/api/info", None)
            self.assertIn("ota_auth", data["capabilities"])
        finally:
            mock_api.ADMIN_PASSWORD = previous

    def test_grill_has_uptime_and_cook_session(self):
        _, data = mock_api.handle("GET", "/api/grill", None)
        self.assertIsInstance(data["uptime_seconds"], int)
        self.assertEqual("c-1a2b3c4d-0001", data["cook_session"]["id"])


class EventsTest(unittest.TestCase):
    def test_sse_frame_contains_one_json_data_message(self):
        frame = mock_api.sse_frame({"uptime_seconds": 1})
        self.assertTrue(frame.startswith("data: "))
        self.assertTrue(frame.endswith("\n\n"))
        self.assertEqual({"uptime_seconds": 1}, json.loads(frame[6:-2]))


class SettingsTest(unittest.TestCase):
    def test_power_saving_can_be_updated(self):
        status, data = mock_api.handle("POST", "/api/settings", b'{"power_saving": true}')
        self.assertEqual(200, status)
        self.assertTrue(data["power_saving"])

    def test_power_saving_must_be_a_boolean(self):
        status, data = mock_api.handle("POST", "/api/settings", b'{"power_saving": "true"}')
        self.assertEqual(400, status)
        self.assertEqual("power_saving should be true or false", data["error"])


class AlarmMuteTest(unittest.TestCase):
    def setUp(self):
        mock_api.ALARM_SOUNDING = True
        mock_api.ALARM_PROBE_ID = 2

    def tearDown(self):
        mock_api.ALARM_SOUNDING = False
        mock_api.ALARM_PROBE_ID = None

    def test_mutes_one_sounding_probe(self):
        status, data = mock_api.handle("POST", "/api/probes/2/alarm/mute", b"{}", {"Content-Type": "application/json"})
        self.assertEqual(200, status)
        self.assertEqual({"success": True}, data)
        self.assertFalse(mock_api.ALARM_SOUNDING)
        self.assertIsNone(mock_api.ALARM_PROBE_ID)

    def test_probe_mute_rejects_bad_probe(self):
        status, data = mock_api.handle("POST", "/api/probes/9/alarm/mute", b"{}", {"Content-Type": "application/json"})
        self.assertEqual(400, status)
        self.assertEqual({"error": "probe should be 1 to 8"}, data)


class HistoryTest(unittest.TestCase):
    def test_all_connected_probes_have_coarse_history(self):
        status, data = mock_api.handle("GET", "/api/history", None)
        self.assertEqual(200, status)
        ids = [p["probe_id"] for p in data["probes"]]
        self.assertEqual([1, 2, 3], ids)
        coarse = data["probes"][0]["coarse"]
        self.assertEqual(60, coarse["interval"])
        self.assertTrue(len(coarse["values"]) > 10)
        self.assertNotIn("fine", data["probes"][0])

    def test_one_probe_has_fine_history_too(self):
        status, data = mock_api.handle("GET", "/api/history", None, query="probe=2")
        self.assertEqual(200, status)
        self.assertEqual([2], [p["probe_id"] for p in data["probes"]])
        self.assertEqual(10, data["probes"][0]["fine"]["interval"])
        self.assertEqual(180, len(data["probes"][0]["fine"]["values"]))

    def test_bad_probe_is_rejected(self):
        status, data = mock_api.handle("GET", "/api/history", None, query="probe=9")
        self.assertEqual(400, status)

    def test_clear_empties_one_probe(self):
        json_headers = {"Content-Type": "application/json"}
        status, _ = mock_api.handle("POST", "/api/history/clear", json.dumps({"probe_id": 1}).encode(), json_headers)
        self.assertEqual(200, status)
        _, data = mock_api.handle("GET", "/api/history", None, query="probe=1")
        self.assertEqual([], data["probes"][0]["coarse"]["values"])
        mock_api.CLEARED_AT.pop(1, None)

    def test_probes_report_eta_seconds(self):
        _, data = mock_api.handle("GET", "/api/grill", None)
        self.assertIn("eta_seconds", data["probes"][0])
        self.assertEqual(-1, data["probes"][3]["eta_seconds"])

    def test_age_is_zero_when_values_is_empty(self):
        json_headers = {"Content-Type": "application/json"}
        status, _ = mock_api.handle("POST", "/api/history/clear", json.dumps({"probe_id": 1}).encode(), json_headers)
        self.assertEqual(200, status)
        try:
            _, data = mock_api.handle("GET", "/api/history", None, query="probe=1")
            coarse = data["probes"][0]["coarse"]
            fine = data["probes"][0]["fine"]
            self.assertEqual([], coarse["values"])
            self.assertEqual(0, coarse["age"])
            self.assertEqual([], fine["values"])
            self.assertEqual(0, fine["age"])
        finally:
            mock_api.CLEARED_AT.pop(1, None)

    def test_clear_rejects_invalid_json(self):
        json_headers = {"Content-Type": "application/json"}
        status, data = mock_api.handle("POST", "/api/history/clear", b"not json", json_headers)
        self.assertEqual(400, status)
        self.assertEqual({"error": "Could not deserialize json"}, data)

    def test_clear_rejects_non_object_body(self):
        json_headers = {"Content-Type": "application/json"}
        status, data = mock_api.handle("POST", "/api/history/clear", json.dumps([1, 2]).encode(), json_headers)
        self.assertEqual(400, status)
        self.assertEqual({"error": "Could not deserialize json"}, data)


if __name__ == "__main__":
    unittest.main()
