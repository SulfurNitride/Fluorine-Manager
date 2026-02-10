import pytest

import mobase

m = pytest.importorskip("mobase_tests.organizer")


def test_getters():
    o: mobase.IOrganizer = m.organizer()
    assert o.profileName() == "profile"
    assert o.startApplication("valid.exe") == 4654
    assert o.startApplication("invalid.exe") == mobase.INVALID_HANDLE_VALUE
    assert o.waitForApplication(42) == (False, -1)
    assert o.waitForApplication(4654) == (True, 0)
