"""Tests for CalibrationData model and _mk_defaults factory."""

import copy
import pytest

from calibration_editor import CalibrationData, _mk_defaults


# ---------------------------------------------------------------------------
# _mk_defaults factory
# ---------------------------------------------------------------------------

class TestMkDefaults:
    def test_has_ignition_timing(self):
        d = _mk_defaults()
        assert "ignition_timing" in d

    def test_has_fuel_map(self):
        assert "fuel_map" in _mk_defaults()

    def test_has_boost_control(self):
        assert "boost_control" in _mk_defaults()

    def test_has_rev_limit(self):
        assert "rev_limit" in _mk_defaults()

    def test_ignition_timing_rpm_and_values_same_length(self):
        ign = _mk_defaults()["ignition_timing"]
        assert len(ign["rpm"]) == len(ign["values"])

    def test_ignition_timing_unit(self):
        assert _mk_defaults()["ignition_timing"]["unit"] == "°BTDC"

    def test_fuel_map_row_count_matches_load_count(self):
        fuel = _mk_defaults()["fuel_map"]
        assert len(fuel["values"]) == len(fuel["load"])

    def test_fuel_map_each_row_matches_rpm_count(self):
        fuel = _mk_defaults()["fuel_map"]
        for i, row in enumerate(fuel["values"]):
            assert len(row) == len(fuel["rpm"]), (
                f"Row {i} length {len(row)} != RPM count {len(fuel['rpm'])}"
            )

    def test_fuel_map_unit(self):
        assert _mk_defaults()["fuel_map"]["unit"] == "мс"

    def test_boost_control_rpm_and_values_same_length(self):
        boost = _mk_defaults()["boost_control"]
        assert len(boost["rpm"]) == len(boost["values"])

    def test_rev_limit_hard_greater_than_soft(self):
        rev = _mk_defaults()["rev_limit"]
        assert rev["value"] > rev["soft_limit"]

    def test_rev_limit_unit(self):
        assert _mk_defaults()["rev_limit"]["unit"] == "об/мин"

    def test_returns_independent_copies(self):
        d1 = _mk_defaults()
        d2 = _mk_defaults()
        d1["rev_limit"]["value"] = 9999
        assert d2["rev_limit"]["value"] != 9999


# ---------------------------------------------------------------------------
# CalibrationData initialisation
# ---------------------------------------------------------------------------

class TestCalibrationDataInit:
    def test_default_source_filename_empty(self):
        assert CalibrationData().source_filename == ""

    def test_custom_source_filename(self):
        cd = CalibrationData("backup.json")
        assert cd.source_filename == "backup.json"

    def test_not_dirty_on_creation(self):
        assert CalibrationData().is_dirty is False

    def test_original_and_modified_equal_on_creation(self):
        cd = CalibrationData()
        assert cd.original == cd.modified

    def test_original_and_modified_are_independent_objects(self):
        cd = CalibrationData()
        assert cd.original is not cd.modified

    def test_original_has_all_params(self):
        cd = CalibrationData()
        for key in ("ignition_timing", "fuel_map", "boost_control", "rev_limit"):
            assert key in cd.original

    def test_modified_has_all_params(self):
        cd = CalibrationData()
        for key in ("ignition_timing", "fuel_map", "boost_control", "rev_limit"):
            assert key in cd.modified


# ---------------------------------------------------------------------------
# is_dirty property
# ---------------------------------------------------------------------------

class TestIsDirty:
    def test_false_after_init(self):
        assert CalibrationData().is_dirty is False

    def test_true_after_apply(self):
        cd = CalibrationData()
        new_rev = copy.deepcopy(cd.original["rev_limit"])
        new_rev["value"] = 7500
        cd.apply("rev_limit", new_rev)
        assert cd.is_dirty is True

    def test_false_after_apply_and_reset_single(self):
        cd = CalibrationData()
        new_rev = copy.deepcopy(cd.original["rev_limit"])
        new_rev["value"] = 7500
        cd.apply("rev_limit", new_rev)
        cd.reset("rev_limit")
        assert cd.is_dirty is False

    def test_false_after_apply_and_reset_all(self):
        cd = CalibrationData()
        new_rev = copy.deepcopy(cd.original["rev_limit"])
        new_rev["value"] = 7500
        cd.apply("rev_limit", new_rev)
        new_ign = copy.deepcopy(cd.original["ignition_timing"])
        new_ign["values"][0] = 99.0
        cd.apply("ignition_timing", new_ign)
        cd.reset_all()
        assert cd.is_dirty is False


# ---------------------------------------------------------------------------
# apply()
# ---------------------------------------------------------------------------

class TestApply:
    def test_apply_changes_modified(self):
        cd = CalibrationData()
        orig_value = cd.modified["rev_limit"]["value"]
        new_rev = copy.deepcopy(cd.original["rev_limit"])
        new_rev["value"] = orig_value + 500
        cd.apply("rev_limit", new_rev)
        assert cd.modified["rev_limit"]["value"] == orig_value + 500

    def test_apply_does_not_change_original(self):
        cd = CalibrationData()
        orig_value = cd.original["rev_limit"]["value"]
        new_rev = copy.deepcopy(cd.original["rev_limit"])
        new_rev["value"] = 9999
        cd.apply("rev_limit", new_rev)
        assert cd.original["rev_limit"]["value"] == orig_value

    def test_apply_stores_deep_copy(self):
        cd = CalibrationData()
        new_rev = copy.deepcopy(cd.original["rev_limit"])
        new_rev["value"] = 7000
        cd.apply("rev_limit", new_rev)
        new_rev["value"] = 8000  # mutate the passed dict after apply
        assert cd.modified["rev_limit"]["value"] == 7000

    def test_apply_ignition_timing_values(self):
        cd = CalibrationData()
        new_ign = copy.deepcopy(cd.original["ignition_timing"])
        new_ign["values"] = [v + 1 for v in new_ign["values"]]
        cd.apply("ignition_timing", new_ign)
        assert cd.modified["ignition_timing"]["values"][0] == pytest.approx(
            cd.original["ignition_timing"]["values"][0] + 1
        )


# ---------------------------------------------------------------------------
# reset()
# ---------------------------------------------------------------------------

class TestResetSingle:
    def test_reset_reverts_one_param(self):
        cd = CalibrationData()
        new_rev = copy.deepcopy(cd.original["rev_limit"])
        new_rev["value"] = 7500
        cd.apply("rev_limit", new_rev)
        cd.reset("rev_limit")
        assert cd.modified["rev_limit"] == cd.original["rev_limit"]

    def test_reset_does_not_affect_other_params(self):
        cd = CalibrationData()
        new_ign = copy.deepcopy(cd.original["ignition_timing"])
        new_ign["values"][0] = 99.0
        cd.apply("ignition_timing", new_ign)
        # Reset only rev_limit (which was not changed)
        cd.reset("rev_limit")
        # ignition_timing change must still be present
        assert cd.modified["ignition_timing"]["values"][0] == pytest.approx(99.0)
        assert cd.is_dirty is True

    def test_reset_stores_independent_copy(self):
        cd = CalibrationData()
        cd.reset("rev_limit")
        cd.modified["rev_limit"]["value"] = 9999
        assert cd.original["rev_limit"]["value"] != 9999


# ---------------------------------------------------------------------------
# reset_all()
# ---------------------------------------------------------------------------

class TestResetAll:
    def test_reset_all_clears_all_modifications(self):
        cd = CalibrationData()
        for param in ("rev_limit", "ignition_timing", "boost_control"):
            data = copy.deepcopy(cd.original[param])
            if "value" in data:
                data["value"] = 9999
            elif "values" in data:
                data["values"] = [0.0] * len(data["values"])
            cd.apply(param, data)
        cd.reset_all()
        assert not cd.is_dirty

    def test_reset_all_matches_original(self):
        cd = CalibrationData()
        new_rev = copy.deepcopy(cd.original["rev_limit"])
        new_rev["value"] = 8000
        cd.apply("rev_limit", new_rev)
        cd.reset_all()
        assert cd.modified == cd.original

    def test_reset_all_creates_independent_copy(self):
        cd = CalibrationData()
        cd.reset_all()
        cd.modified["rev_limit"]["value"] = 9999
        assert cd.original["rev_limit"]["value"] != 9999
