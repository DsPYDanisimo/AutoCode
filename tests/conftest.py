import sys
import os

# Make front/ importable from any test
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "front"))

# QApplication must exist before any QObject/QThread/QWidget is instantiated.
# Create it once at collection time so fixtures can safely import Qt classes.
from PyQt5.QtWidgets import QApplication

_qapp = QApplication.instance() or QApplication(sys.argv)


import pytest


@pytest.fixture(scope="session")
def qapp():
    """Session-wide QApplication instance."""
    return _qapp
