"""Tests for ECUMonitorThread logic (no real serial hardware required)."""

import pytest
from unittest.mock import MagicMock, patch


# ---------------------------------------------------------------------------
# Fixture
# ---------------------------------------------------------------------------

@pytest.fixture
def monitor(qapp):
    from ecu_monitor import ECUMonitorThread
    m = ECUMonitorThread()
    yield m
    # Stop without starting — QThread.wait() returns immediately when not running
    m.running = False


def _make_port(device="COM1", description="USB Serial Port",
               manufacturer="FTDI", hwid="USB VID:PID=0403:6001",
               vid=0x0403, pid=0x6001):
    """Build a mock serial port object."""
    p = MagicMock()
    p.device = device
    p.description = description
    p.manufacturer = manufacturer
    p.hwid = hwid
    p.vid = vid
    p.pid = pid
    p.serial_number = None
    p.location = None
    p.interface = None
    return p


# ---------------------------------------------------------------------------
# is_obd_device()
# ---------------------------------------------------------------------------

class TestIsObdDevice:
    def test_obd_keyword_in_description(self, monitor):
        port = _make_port(description="OBD-II USB Adapter", vid=None)
        assert monitor.is_obd_device(port) is True

    def test_elm327_keyword_in_description(self, monitor):
        port = _make_port(description="ELM327 USB Interface", vid=None)
        assert monitor.is_obd_device(port) is True

    def test_vgate_keyword(self, monitor):
        port = _make_port(description="Vgate iCar", manufacturer="Vgate", vid=None)
        assert monitor.is_obd_device(port) is True

    def test_diagnostic_keyword(self, monitor):
        port = _make_port(description="Diagnostic Tool USB", vid=None)
        assert monitor.is_obd_device(port) is True

    def test_obd2_keyword(self, monitor):
        port = _make_port(description="OBD2 Scanner", vid=None)
        assert monitor.is_obd_device(port) is True

    def test_ftdi_vid_recognized(self, monitor):
        port = _make_port(description="Standard Port", vid=0x0403)
        assert monitor.is_obd_device(port) is True

    def test_prolific_vid_recognized(self, monitor):
        port = _make_port(description="Standard Port", vid=0x067B)
        assert monitor.is_obd_device(port) is True

    def test_ch340_vid_recognized(self, monitor):
        port = _make_port(description="Standard Port", vid=0x1A86)
        assert monitor.is_obd_device(port) is True

    def test_silicon_labs_vid_recognized(self, monitor):
        port = _make_port(description="Standard Port", vid=0x10C4)
        assert monitor.is_obd_device(port) is True

    def test_generic_port_not_obd(self, monitor):
        port = _make_port(description="Standard COM Port",
                          manufacturer="Microsoft",
                          hwid="", vid=None)
        assert monitor.is_obd_device(port) is False

    def test_none_description_and_manufacturer(self, monitor):
        port = _make_port(description=None, manufacturer=None, hwid=None, vid=None)
        assert monitor.is_obd_device(port) is False

    def test_keyword_in_hwid(self, monitor):
        port = _make_port(description="Generic Port",
                          manufacturer="Unknown",
                          hwid="USB VID:PID=1234:5678 obd",
                          vid=None)
        assert monitor.is_obd_device(port) is True

    def test_keyword_in_manufacturer(self, monitor):
        port = _make_port(description="Generic Port",
                          manufacturer="ELM327 Inc",
                          hwid="", vid=None)
        assert monitor.is_obd_device(port) is True


# ---------------------------------------------------------------------------
# detect_protocol_from_description()
# ---------------------------------------------------------------------------

class TestDetectProtocol:
    def _pi(self, description, manufacturer=""):
        return {"description": description, "manufacturer": manufacturer}

    def test_can_keyword(self, monitor):
        assert monitor.detect_protocol_from_description(self._pi("CAN Bus Adapter")) == "CAN"

    def test_obd_keyword(self, monitor):
        assert monitor.detect_protocol_from_description(self._pi("OBD-II USB")) == "OBD-II"

    def test_kwp_keyword(self, monitor):
        assert monitor.detect_protocol_from_description(self._pi("KWP Interface")) == "KWP2000"

    def test_2000_keyword(self, monitor):
        assert monitor.detect_protocol_from_description(self._pi("Protocol 2000")) == "KWP2000"

    def test_uds_keyword(self, monitor):
        assert monitor.detect_protocol_from_description(self._pi("UDS Diagnostic Tool")) == "UDS"

    def test_j1850_keyword(self, monitor):
        result = monitor.detect_protocol_from_description(self._pi("J1850 PWM Adapter"))
        assert result == "J1850"

    def test_pwm_keyword_maps_to_j1850(self, monitor):
        result = monitor.detect_protocol_from_description(self._pi("PWM USB Interface"))
        assert result == "J1850"

    def test_vpw_keyword_maps_to_j1850(self, monitor):
        result = monitor.detect_protocol_from_description(self._pi("VPW Adapter"))
        assert result == "J1850"

    def test_iso9141_keyword(self, monitor):
        result = monitor.detect_protocol_from_description(self._pi("ISO9141 Interface"))
        assert result == "ISO9141"

    def test_9141_keyword(self, monitor):
        result = monitor.detect_protocol_from_description(self._pi("9141 Serial"))
        assert result == "ISO9141"

    def test_elm327_returns_supported_protocol(self, monitor):
        result = monitor.detect_protocol_from_description(self._pi("ELM327 USB"))
        assert result in ("OBD-II", "CAN", "J1850", "ISO9141")

    def test_unknown_device_defaults_to_obd2(self, monitor):
        result = monitor.detect_protocol_from_description(self._pi("Unknown USB Device"))
        assert result == "OBD-II"

    def test_empty_description_defaults_to_obd2(self, monitor):
        result = monitor.detect_protocol_from_description(self._pi(""))
        assert result == "OBD-II"

    def test_manufacturer_contributes_to_detection(self, monitor):
        pi = {"description": "", "manufacturer": "CAN-Tech"}
        result = monitor.detect_protocol_from_description(pi)
        assert result == "CAN"


# ---------------------------------------------------------------------------
# check_ecu_connection()
# ---------------------------------------------------------------------------

class TestCheckEcuConnection:
    def test_empty_ports_returns_not_connected(self, monitor):
        result = monitor.check_ecu_connection([])
        assert result["connected"] is False
        assert result["port"] == "NONE"
        assert result["protocol"] == "NONE"

    def test_empty_ports_vin_empty(self, monitor):
        result = monitor.check_ecu_connection([])
        assert result.get("vin", "") == ""

    def test_obd_port_serial_success_returns_connected(self, monitor):
        port_info = {
            "device": "COM3",
            "is_obd": True,
            "manufacturer": "FTDI",
            "description": "OBD USB",
        }
        mock_ser = MagicMock()
        with patch("serial.Serial", return_value=mock_ser):
            result = monitor.check_ecu_connection([port_info])
        assert result["connected"] is True
        assert result["port"] == "COM3"
        mock_ser.close.assert_called_once()

    def test_obd_port_protocol_detected(self, monitor):
        port_info = {
            "device": "COM3",
            "is_obd": True,
            "manufacturer": "FTDI",
            "description": "CAN Bus OBD",
        }
        with patch("serial.Serial", return_value=MagicMock()):
            result = monitor.check_ecu_connection([port_info])
        assert result["protocol"] in monitor.supported_protocols

    def test_obd_port_serial_error_uses_first_port_fallback(self, monitor):
        """If serial.Serial raises, the code falls through to the demo fallback."""
        port_info = {
            "device": "COM3",
            "is_obd": True,
            "manufacturer": "FTDI",
            "description": "OBD USB",
        }
        with patch("serial.Serial", side_effect=Exception("port busy")):
            result = monitor.check_ecu_connection([port_info])
        # Demo fallback: first port in list → connected
        assert result["connected"] is True

    def test_non_obd_port_uses_demo_fallback(self, monitor):
        port_info = {
            "device": "COM1",
            "is_obd": False,
            "manufacturer": "Unknown",
            "description": "Standard COM",
        }
        result = monitor.check_ecu_connection([port_info])
        assert result["connected"] is True
        assert result["port"] == "COM1"

    def test_result_contains_required_keys(self, monitor):
        result = monitor.check_ecu_connection([])
        for key in ("connected", "port", "protocol", "ecu_type", "vin"):
            assert key in result, f"Missing key: {key}"

    def test_multiple_ports_first_obd_wins(self, monitor):
        ports = [
            {"device": "COM1", "is_obd": False, "manufacturer": "", "description": ""},
            {"device": "COM2", "is_obd": True,  "manufacturer": "FTDI", "description": "OBD"},
            {"device": "COM3", "is_obd": True,  "manufacturer": "FTDI", "description": "OBD"},
        ]
        with patch("serial.Serial", return_value=MagicMock()):
            result = monitor.check_ecu_connection(ports)
        assert result["connected"] is True
        assert result["port"] == "COM2"


# ---------------------------------------------------------------------------
# scan_serial_ports() — mocked comports
# ---------------------------------------------------------------------------

class TestScanSerialPorts:
    def test_returns_list(self, monitor):
        with patch("serial.tools.list_ports.comports", return_value=[]):
            ports = monitor.scan_serial_ports()
        assert isinstance(ports, list)

    def test_empty_when_no_ports(self, monitor):
        with patch("serial.tools.list_ports.comports", return_value=[]):
            assert monitor.scan_serial_ports() == []

    def test_port_dict_has_required_keys(self, monitor):
        mock_port = _make_port("COM4", "USB Serial Port")
        with patch("serial.tools.list_ports.comports", return_value=[mock_port]):
            ports = monitor.scan_serial_ports()
        assert len(ports) == 1
        for key in ("device", "description", "manufacturer", "is_obd", "type"):
            assert key in ports[0], f"Missing key: {key}"

    def test_bluetooth_port_type(self, monitor):
        mock_port = _make_port(description="Bluetooth Serial Port", vid=None)
        with patch("serial.tools.list_ports.comports", return_value=[mock_port]):
            ports = monitor.scan_serial_ports()
        assert ports[0]["type"] == "bluetooth"

    def test_usb_port_type(self, monitor):
        mock_port = _make_port(description="USB Serial Port", vid=None)
        with patch("serial.tools.list_ports.comports", return_value=[mock_port]):
            ports = monitor.scan_serial_ports()
        assert ports[0]["type"] == "usb"

    def test_plain_serial_port_type(self, monitor):
        mock_port = _make_port(description="COM Port", vid=None)
        with patch("serial.tools.list_ports.comports", return_value=[mock_port]):
            ports = monitor.scan_serial_ports()
        assert ports[0]["type"] == "serial"

    def test_multiple_ports_returned(self, monitor):
        mock_ports = [_make_port(f"COM{i}", "USB Serial Port") for i in range(3)]
        with patch("serial.tools.list_ports.comports", return_value=mock_ports):
            ports = monitor.scan_serial_ports()
        assert len(ports) == 3

    def test_comports_exception_returns_empty(self, monitor):
        with patch("serial.tools.list_ports.comports", side_effect=Exception("permission")):
            ports = monitor.scan_serial_ports()
        assert ports == []


# ---------------------------------------------------------------------------
# Supported-protocols list
# ---------------------------------------------------------------------------

class TestSupportedProtocols:
    def test_contains_six_protocols(self, monitor):
        assert len(monitor.supported_protocols) == 6

    def test_all_expected_protocols_present(self, monitor):
        for proto in ("OBD-II", "CAN", "KWP2000", "UDS", "J1850", "ISO9141"):
            assert proto in monitor.supported_protocols
