import json
import unittest

import mock_api


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
