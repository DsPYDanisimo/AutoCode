"""Tests for ECUAPIClient — all HTTP calls are mocked."""

import threading
import pytest
import requests
from unittest.mock import MagicMock, patch
from datetime import datetime, timedelta


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def client(qapp):
    from api_client import ECUAPIClient
    c = ECUAPIClient(host="localhost", port=8080)
    yield c


def _ok_response(data):
    """Return a mock requests.Response with status 200 and given JSON payload."""
    r = MagicMock()
    r.status_code = 200
    r.json.return_value = data
    return r


def _err_response(status=500):
    r = MagicMock()
    r.status_code = status
    return r


# ---------------------------------------------------------------------------
# __init__ / base state
# ---------------------------------------------------------------------------

class TestInitialisation:
    def test_base_url_default(self, client):
        assert client.base_url == "http://localhost:8080/api"

    def test_custom_host_port(self, qapp):
        from api_client import ECUAPIClient
        c = ECUAPIClient(host="192.168.0.1", port=9090)
        assert c.base_url == "http://192.168.0.1:9090/api"

    def test_is_connected_false(self, client):
        assert client.is_connected is False

    def test_signal_quality_zero(self, client):
        assert client.signal_quality == 0

    def test_reconnect_attempts_zero(self, client):
        assert client.reconnect_attempts == 0

    def test_dtc_cache_empty(self, client):
        assert client.dtc_cache == {}

    def test_j2534_none(self, client):
        assert client._j2534 is None

    def test_current_connection_not_connected(self, client):
        assert client.current_connection["connected"] is False


# ---------------------------------------------------------------------------
# is_connected property (thread-safety)
# ---------------------------------------------------------------------------

class TestIsConnectedProperty:
    def test_set_true(self, client):
        client.is_connected = True
        assert client.is_connected is True

    def test_set_false(self, client):
        client.is_connected = False
        assert client.is_connected is False

    def test_concurrent_writes_do_not_raise(self, client):
        errors = []

        def toggle(val):
            try:
                client.is_connected = val
                _ = client.is_connected
            except Exception as e:
                errors.append(e)

        threads = [threading.Thread(target=toggle, args=(i % 2 == 0,)) for i in range(20)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        assert errors == []


# ---------------------------------------------------------------------------
# get_connection_status()
# ---------------------------------------------------------------------------

class TestGetConnectionStatus:
    def test_success_returns_data(self, client):
        with patch("requests.get", return_value=_ok_response(
                {"connected": True, "protocol": "OBD-II", "port": "COM3"})):
            s = client.get_connection_status()
        assert s["connected"] is True
        assert s["protocol"] == "OBD-II"

    def test_connection_error_returns_not_connected(self, client):
        with patch("requests.get", side_effect=requests.exceptions.ConnectionError):
            s = client.get_connection_status()
        assert s["connected"] is False

    def test_timeout_returns_not_connected(self, client):
        with patch("requests.get", side_effect=requests.exceptions.Timeout):
            s = client.get_connection_status()
        assert s["connected"] is False

    def test_generic_exception_returns_not_connected(self, client):
        with patch("requests.get", side_effect=Exception("unexpected")):
            s = client.get_connection_status()
        assert s["connected"] is False

    def test_non_200_status_returns_not_connected(self, client):
        with patch("requests.get", return_value=_err_response(503)):
            s = client.get_connection_status()
        assert s["connected"] is False

    def test_signal_quality_injected_into_response(self, client):
        client.signal_quality = 75
        with patch("requests.get", return_value=_ok_response(
                {"connected": True, "protocol": "CAN", "port": "COM3"})):
            s = client.get_connection_status()
        assert s["signal_quality"] == 75

    def test_j2534_mode_bypasses_http(self, client):
        mock_j2534 = MagicMock()
        mock_j2534.is_connected.return_value = True
        client._j2534 = mock_j2534
        try:
            with patch("requests.get") as mock_get:
                s = client.get_connection_status()
                mock_get.assert_not_called()
            assert s["connected"] is True
            assert s["protocol"] == "J2534/ISO15765"
        finally:
            client._j2534 = None

    def test_j2534_disconnected_clears_j2534_ref(self, client):
        mock_j2534 = MagicMock()
        mock_j2534.is_connected.return_value = False
        client._j2534 = mock_j2534
        client.get_connection_status()
        assert client._j2534 is None


# ---------------------------------------------------------------------------
# connect_to_port()
# ---------------------------------------------------------------------------

class TestConnectToPort:
    def _status_resp(self, connected=False):
        return _ok_response({"connected": connected, "protocol": "None", "port": "None"})

    def test_success_sets_is_connected(self, client):
        with patch("requests.get", return_value=self._status_resp(False)), \
             patch("requests.post", return_value=_ok_response({"success": True})):
            result = client.connect_to_port({"name": "COM3", "type": "serial"})
        assert result["success"] is True
        assert client.is_connected is True

    def test_server_unavailable_returns_error(self, client):
        with patch("requests.get", side_effect=requests.exceptions.ConnectionError), \
             patch("requests.post", side_effect=requests.exceptions.ConnectionError):
            result = client.connect_to_port({"name": "COM3", "type": "serial"})
        assert result["success"] is False
        assert "error" in result

    def test_timeout_returns_error(self, client):
        with patch("requests.get", return_value=self._status_resp(False)), \
             patch("requests.post", side_effect=requests.exceptions.Timeout):
            result = client.connect_to_port({"name": "COM3", "type": "serial"})
        assert result["success"] is False

    def test_http_500_returns_error(self, client):
        with patch("requests.get", return_value=self._status_resp(False)), \
             patch("requests.post", return_value=_err_response(500)):
            result = client.connect_to_port({"name": "COM3", "type": "serial"})
        assert result["success"] is False

    def test_server_reports_failure(self, client):
        with patch("requests.get", return_value=self._status_resp(False)), \
             patch("requests.post", return_value=_ok_response(
                 {"success": False, "error": "Protocol not found"})):
            result = client.connect_to_port({"name": "COM3", "type": "serial"})
        assert result["success"] is False


# ---------------------------------------------------------------------------
# disconnect()
# ---------------------------------------------------------------------------

class TestDisconnect:
    def test_clears_is_connected(self, client):
        client._is_connected = True
        with patch("requests.post", return_value=_ok_response({"success": True})):
            client.disconnect()
        assert client.is_connected is False

    def test_resets_signal_quality(self, client):
        client._is_connected = True
        client.signal_quality = 80
        with patch("requests.post", return_value=_ok_response({"success": True})):
            client.disconnect()
        assert client.signal_quality == 0

    def test_resets_connection_start_time(self, client):
        client._is_connected = True
        client.connection_start_time = datetime.now()
        with patch("requests.post", return_value=_ok_response({"success": True})):
            client.disconnect()
        assert client.connection_start_time is None

    def test_j2534_disconnect_called(self, client):
        mock_j2534 = MagicMock()
        client._j2534 = mock_j2534
        client._is_connected = True
        client.disconnect()
        mock_j2534.disconnect.assert_called_once()
        assert client._j2534 is None

    def test_j2534_disconnect_clears_state(self, client):
        mock_j2534 = MagicMock()
        client._j2534 = mock_j2534
        client._is_connected = True
        client.disconnect()
        assert client.is_connected is False
        assert client.signal_quality == 0

    def test_server_error_still_clears_state(self, client):
        client._is_connected = True
        with patch("requests.post", side_effect=Exception("network down")):
            client.disconnect()
        assert client.is_connected is False

    def test_returns_true_on_success(self, client):
        with patch("requests.post", return_value=_ok_response({"success": True})):
            result = client.disconnect()
        assert result is True


# ---------------------------------------------------------------------------
# read_error_codes()
# ---------------------------------------------------------------------------

class TestReadErrorCodes:
    def test_not_connected_returns_empty(self, client):
        client._is_connected = False
        result = client.read_error_codes()
        assert result == {"errors": [], "count": 0}

    def test_connected_returns_errors_from_server(self, client):
        client._is_connected = True
        with patch("requests.get", return_value=_ok_response(
                {"errors": ["P0001", "P0002"], "count": 2})):
            result = client.read_error_codes()
        assert result["count"] == 2
        assert "P0001" in result["errors"]

    def test_connected_server_error_returns_empty(self, client):
        client._is_connected = True
        with patch("requests.get", side_effect=Exception("timeout")):
            result = client.read_error_codes()
        assert result == {"errors": [], "count": 0}

    def test_j2534_path_calls_read_dtc(self, client):
        client._is_connected = True
        mock_j2534 = MagicMock()
        mock_j2534.read_dtc.return_value = ["P0300", "P0301"]
        client._j2534 = mock_j2534
        try:
            result = client.read_error_codes()
            mock_j2534.read_dtc.assert_called_once()
            assert result["count"] == 2
        finally:
            client._j2534 = None

    def test_j2534_read_dtc_exception_returns_empty(self, client):
        client._is_connected = True
        mock_j2534 = MagicMock()
        mock_j2534.read_dtc.side_effect = Exception("CAN error")
        client._j2534 = mock_j2534
        try:
            result = client.read_error_codes()
            assert result == {"errors": [], "count": 0}
        finally:
            client._j2534 = None


# ---------------------------------------------------------------------------
# get_error_description()
# ---------------------------------------------------------------------------

class TestGetErrorDescription:
    def test_cache_hit_no_request(self, client):
        client.dtc_cache["P0001"] = {"code": "P0001", "description": "Fuel pump"}
        with patch("requests.get") as mock_get:
            result = client.get_error_description("P0001")
            mock_get.assert_not_called()
        assert result["description"] == "Fuel pump"

    def test_cache_miss_fetches_from_server(self, client):
        client.dtc_cache.pop("P0002", None)
        with patch("requests.get", return_value=_ok_response(
                {"code": "P0002", "description": "Injector fault"})):
            result = client.get_error_description("P0002")
        assert result["code"] == "P0002"

    def test_successful_fetch_populates_cache(self, client):
        client.dtc_cache.pop("P0003", None)
        with patch("requests.get", return_value=_ok_response(
                {"code": "P0003", "description": "Misfire"})):
            client.get_error_description("P0003")
        assert "P0003" in client.dtc_cache

    def test_server_error_returns_unknown_dtc(self, client):
        client.dtc_cache.pop("P9999", None)
        with patch("requests.get", side_effect=Exception("network error")):
            result = client.get_error_description("P9999")
        assert result["code"] == "P9999"
        assert "Unknown" in result["description"]

    def test_server_error_does_not_cache(self, client):
        client.dtc_cache.pop("P8888", None)
        with patch("requests.get", side_effect=Exception("error")):
            client.get_error_description("P8888")
        assert "P8888" not in client.dtc_cache


# ---------------------------------------------------------------------------
# get_all_dtc_codes() / search_dtc_codes()
# ---------------------------------------------------------------------------

class TestDtcListing:
    def test_get_all_returns_list(self, client):
        data = [{"code": "P0001"}, {"code": "P0002"}]
        with patch("requests.get", return_value=_ok_response(data)):
            result = client.get_all_dtc_codes()
        assert isinstance(result, list)
        assert len(result) == 2

    def test_get_all_populates_cache(self, client):
        data = [{"code": "P0010", "description": "VVT fault"}]
        with patch("requests.get", return_value=_ok_response(data)):
            client.get_all_dtc_codes()
        assert "P0010" in client.dtc_cache

    def test_get_all_server_error_returns_empty(self, client):
        with patch("requests.get", side_effect=Exception("error")):
            result = client.get_all_dtc_codes()
        assert result == []

    def test_search_returns_list_on_success(self, client):
        with patch("requests.get", return_value=_ok_response([{"code": "P0001"}])):
            result = client.search_dtc_codes("P000")
        assert len(result) == 1

    def test_search_returns_empty_on_error(self, client):
        with patch("requests.get", side_effect=Exception("error")):
            result = client.search_dtc_codes("P000")
        assert result == []


# ---------------------------------------------------------------------------
# clear_errors()
# ---------------------------------------------------------------------------

class TestClearErrors:
    def test_not_connected_returns_false(self, client):
        client._is_connected = False
        assert client.clear_errors() is False

    def test_connected_success(self, client):
        client._is_connected = True
        with patch("requests.post", return_value=_ok_response({"success": True})):
            result = client.clear_errors()
        assert result is True

    def test_connected_server_returns_failure(self, client):
        client._is_connected = True
        with patch("requests.post", return_value=_ok_response({"success": False})):
            result = client.clear_errors()
        assert result is False

    def test_connected_request_exception(self, client):
        client._is_connected = True
        with patch("requests.post", side_effect=Exception("timeout")):
            result = client.clear_errors()
        assert result is False

    def test_j2534_calls_clear_dtc(self, client):
        client._is_connected = True
        mock_j2534 = MagicMock()
        mock_j2534.clear_dtc.return_value = True
        client._j2534 = mock_j2534
        try:
            result = client.clear_errors()
            mock_j2534.clear_dtc.assert_called_once()
            assert result is True
        finally:
            client._j2534 = None


# ---------------------------------------------------------------------------
# get_realtime_data()
# ---------------------------------------------------------------------------

class TestGetRealtimeData:
    def test_not_connected_returns_empty_dict(self, client):
        client._is_connected = False
        assert client.get_realtime_data() == {}

    def test_connected_returns_live_data(self, client):
        client._is_connected = True
        payload = {"rpm": 1500, "speed": 60, "coolant_temp": 90}
        with patch("requests.get", return_value=_ok_response(payload)):
            result = client.get_realtime_data()
        assert result["rpm"] == 1500
        assert result["speed"] == 60

    def test_connected_request_error_returns_empty(self, client):
        client._is_connected = True
        with patch("requests.get", side_effect=Exception("timeout")):
            result = client.get_realtime_data()
        assert result == {}

    def test_j2534_calls_read_realtime_data(self, client):
        client._is_connected = True
        mock_j2534 = MagicMock()
        mock_j2534.read_realtime_data.return_value = {"rpm": 2000}
        client._j2534 = mock_j2534
        try:
            result = client.get_realtime_data()
            mock_j2534.read_realtime_data.assert_called_once()
            assert result["rpm"] == 2000
        finally:
            client._j2534 = None


# ---------------------------------------------------------------------------
# get_ecu_info()
# ---------------------------------------------------------------------------

class TestGetEcuInfo:
    def test_not_connected_returns_empty(self, client):
        client._is_connected = False
        assert client.get_ecu_info() == {}

    def test_connected_returns_info(self, client):
        client._is_connected = True
        with patch("requests.get", return_value=_ok_response(
                {"VIN": "1HGBH41JXMN109186", "SW": "v1.0"})):
            result = client.get_ecu_info()
        assert "VIN" in result

    def test_connected_request_error_returns_empty(self, client):
        client._is_connected = True
        with patch("requests.get", side_effect=Exception("error")):
            result = client.get_ecu_info()
        assert result == {}

    def test_j2534_returns_interface_key(self, client):
        client._is_connected = True
        mock_j2534 = MagicMock()
        mock_j2534.read_version.return_value = {"firmware": "1.2", "dll": "2.0", "api": "04.04"}
        mock_j2534.read_ecu_info.return_value = {"VIN": "TESTVINNUMBER"}
        client._j2534 = mock_j2534
        try:
            result = client.get_ecu_info()
            assert result.get("Интерфейс") == "J2534 PassThru"
        finally:
            client._j2534 = None


# ---------------------------------------------------------------------------
# _measure_j2534_quality()
# ---------------------------------------------------------------------------

class TestMeasureJ2534Quality:
    def _mock_j2534(self, rtt):
        m = MagicMock()
        m.ping.return_value = rtt
        return m

    def test_rtt_under_20ms_gives_100(self, client):
        assert client._measure_j2534_quality(self._mock_j2534(10)) == 100

    def test_rtt_20_to_49ms_gives_90(self, client):
        assert client._measure_j2534_quality(self._mock_j2534(35)) == 90

    def test_rtt_50_to_99ms_gives_80(self, client):
        assert client._measure_j2534_quality(self._mock_j2534(75)) == 80

    def test_rtt_100_to_199ms_gives_70(self, client):
        assert client._measure_j2534_quality(self._mock_j2534(150)) == 70

    def test_rtt_200_to_499ms_gives_55(self, client):
        assert client._measure_j2534_quality(self._mock_j2534(300)) == 55

    def test_rtt_500_to_999ms_gives_40(self, client):
        assert client._measure_j2534_quality(self._mock_j2534(700)) == 40

    def test_rtt_over_1000ms_gives_30(self, client):
        assert client._measure_j2534_quality(self._mock_j2534(1500)) == 30

    def test_no_response_returns_at_least_30(self, client):
        client.signal_quality = 50
        result = client._measure_j2534_quality(self._mock_j2534(None))
        assert result >= 30

    def test_ping_exception_returns_non_negative(self, client):
        client.signal_quality = 40
        j = MagicMock()
        j.ping.side_effect = Exception("CAN error")
        result = client._measure_j2534_quality(j)
        assert result >= 0


# ---------------------------------------------------------------------------
# _measure_http_quality()
# ---------------------------------------------------------------------------

class TestMeasureHttpQuality:
    def test_server_quality_field_used_when_positive(self, client):
        with patch("requests.get", return_value=_ok_response({"signal_quality": 85})):
            result = client._measure_http_quality()
        assert result == 85

    def test_low_rtt_gives_high_quality(self, client):
        with patch("requests.get", return_value=_ok_response({"signal_quality": 0})):
            result = client._measure_http_quality()
        # Mocked request is near-instant → rtt_ms ≈ 0 → quality = 100
        assert result >= 80

    def test_request_exception_reduces_quality(self, client):
        client.signal_quality = 60
        with patch("requests.get", side_effect=Exception("error")):
            result = client._measure_http_quality()
        assert result >= 0
        assert result <= 60

    def test_non_200_status_reduces_quality(self, client):
        client.signal_quality = 50
        with patch("requests.get", return_value=_err_response(503)):
            result = client._measure_http_quality()
        assert result >= 0


# ---------------------------------------------------------------------------
# get_connection_time()
# ---------------------------------------------------------------------------

class TestGetConnectionTime:
    def test_not_connected_returns_zero_string(self, client):
        client.connection_start_time = None
        assert client.get_connection_time() == "00:00:00"

    def test_connected_returns_elapsed(self, client):
        client.connection_start_time = datetime.now() - timedelta(seconds=70)
        t = client.get_connection_time()
        assert t != "00:00:00"
        assert ":" in t

    def test_connected_format_hh_mm_ss(self, client):
        client.connection_start_time = datetime.now() - timedelta(hours=1, minutes=5, seconds=3)
        t = client.get_connection_time()
        parts = t.split(":")
        assert len(parts) == 3

    def test_no_microseconds_in_result(self, client):
        client.connection_start_time = datetime.now() - timedelta(seconds=10)
        t = client.get_connection_time()
        assert "." not in t


# ---------------------------------------------------------------------------
# Monitoring start / stop (smoke test — no real threads)
# ---------------------------------------------------------------------------

class TestMonitoringLifecycle:
    def test_start_monitoring_sets_flag(self, client):
        client.start_monitoring(interval=100)  # very long interval
        try:
            assert client.is_monitoring is True
        finally:
            client.stop_monitoring()

    def test_stop_monitoring_clears_flag(self, client):
        client.start_monitoring(interval=100)
        client.stop_monitoring()
        assert client.is_monitoring is False
