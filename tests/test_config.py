import json
import os
import pytest
from unittest.mock import patch


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _config():
    from config import Config
    return Config


# ---------------------------------------------------------------------------
# Directory / path constants
# ---------------------------------------------------------------------------

class TestConfigPaths:
    def test_base_dir_is_string(self):
        assert isinstance(_config().BASE_DIR, str)

    def test_data_dir_inside_base(self):
        cfg = _config()
        assert cfg.DATA_DIR.startswith(cfg.BASE_DIR)

    def test_logs_dir_inside_data(self):
        cfg = _config()
        assert cfg.LOGS_DIR.startswith(cfg.DATA_DIR)

    def test_backup_dir_inside_data(self):
        cfg = _config()
        assert cfg.BACKUP_DIR.startswith(cfg.DATA_DIR)

    def test_export_dir_inside_data(self):
        cfg = _config()
        assert cfg.EXPORT_DIR.startswith(cfg.DATA_DIR)

    def test_db_path_ends_with_db_extension(self):
        assert _config().DB_PATH.endswith(".db")


# ---------------------------------------------------------------------------
# Server defaults
# ---------------------------------------------------------------------------

class TestServerDefaults:
    def test_default_host(self):
        assert _config().DEFAULT_HOST == "localhost"

    def test_default_port(self):
        assert _config().DEFAULT_PORT == 8080

    def test_monitor_interval_positive(self):
        assert _config().MONITOR_INTERVAL > 0

    def test_quality_interval_positive(self):
        assert _config().QUALITY_INTERVAL > 0


# ---------------------------------------------------------------------------
# Supported protocols
# ---------------------------------------------------------------------------

class TestSupportedProtocols:
    def test_obd2_present(self):
        assert "OBD-II" in _config().SUPPORTED_PROTOCOLS

    def test_can_present(self):
        assert "CAN" in _config().SUPPORTED_PROTOCOLS

    def test_kwp2000_present(self):
        assert "KWP2000" in _config().SUPPORTED_PROTOCOLS

    def test_uds_present(self):
        assert "UDS" in _config().SUPPORTED_PROTOCOLS

    def test_j1850_present(self):
        assert "J1850" in _config().SUPPORTED_PROTOCOLS

    def test_iso9141_present(self):
        assert "ISO9141" in _config().SUPPORTED_PROTOCOLS


# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------

class TestColors:
    def test_all_required_color_keys_exist(self):
        colors = _config().COLORS
        for key in ("success", "error", "warning", "info", "background", "text"):
            assert key in colors, f"Missing color key: {key}"

    def test_colors_are_hex_strings(self):
        for name, value in _config().COLORS.items():
            assert isinstance(value, str), f"Color '{name}' is not a string"
            assert value.startswith("#"), f"Color '{name}' does not start with '#': {value}"


# ---------------------------------------------------------------------------
# get_log_filename
# ---------------------------------------------------------------------------

class TestGetLogFilename:
    def test_default_prefix_in_filename(self):
        name = _config().get_log_filename()
        basename = os.path.basename(name)
        assert basename.startswith("app")

    def test_custom_prefix_in_filename(self):
        name = _config().get_log_filename("mymodule")
        basename = os.path.basename(name)
        assert basename.startswith("mymodule")

    def test_filename_ends_with_log(self):
        assert _config().get_log_filename().endswith(".log")

    def test_filename_inside_logs_dir(self):
        cfg = _config()
        name = cfg.get_log_filename()
        assert name.startswith(cfg.LOGS_DIR)


# ---------------------------------------------------------------------------
# save_settings / load_settings
# ---------------------------------------------------------------------------

class TestSettingsPersistence:
    def test_save_and_load_roundtrip(self, tmp_path):
        cfg = _config()
        settings = {"theme": "dark", "timeout": 30, "auto_reconnect": True}
        with patch.object(cfg, "DATA_DIR", str(tmp_path)):
            assert cfg.save_settings(settings) is True
            loaded = cfg.load_settings()
        assert loaded == settings

    def test_load_returns_empty_when_file_missing(self, tmp_path):
        cfg = _config()
        with patch.object(cfg, "DATA_DIR", str(tmp_path)):
            result = cfg.load_settings()
        assert result == {}

    def test_save_returns_false_on_bad_path(self):
        cfg = _config()
        bad_dir = "/nonexistent/path/that/cannot/be/created_xyz"
        with patch.object(cfg, "DATA_DIR", bad_dir):
            result = cfg.save_settings({"key": "value"})
        assert result is False

    def test_save_overwrites_existing_settings(self, tmp_path):
        cfg = _config()
        with patch.object(cfg, "DATA_DIR", str(tmp_path)):
            cfg.save_settings({"version": 1})
            cfg.save_settings({"version": 2})
            loaded = cfg.load_settings()
        assert loaded == {"version": 2}

    def test_saved_file_is_valid_json(self, tmp_path):
        cfg = _config()
        settings = {"nested": {"key": [1, 2, 3]}}
        with patch.object(cfg, "DATA_DIR", str(tmp_path)):
            cfg.save_settings(settings)
            settings_file = os.path.join(str(tmp_path), "settings.json")
            with open(settings_file, encoding="utf-8") as f:
                parsed = json.load(f)
        assert parsed == settings

    def test_ensure_dirs_creates_directories(self, tmp_path):
        cfg = _config()
        sub_data = str(tmp_path / "data2")
        sub_logs = str(tmp_path / "data2" / "logs")
        with patch.object(cfg, "DATA_DIR", sub_data), \
             patch.object(cfg, "LOGS_DIR", sub_logs), \
             patch.object(cfg, "BACKUP_DIR", str(tmp_path / "data2" / "bak")), \
             patch.object(cfg, "EXPORT_DIR", str(tmp_path / "data2" / "exp")):
            cfg.ensure_dirs()
        assert os.path.isdir(sub_data)
        assert os.path.isdir(sub_logs)
