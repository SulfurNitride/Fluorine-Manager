from PyQt6.QtCore import QDateTime, Qt

import mobase_tests.qt as m


def test_qstring():
    assert m.qstring_to_stdstring("test") == "test"
    assert m.stdstring_to_qstring("test") == "test"

    assert m.qstring_to_stdstring("éàüö") == "éàüö"
    assert m.stdstring_to_qstring("éàüö") == "éàüö"
    assert m.qstring_to_stdstring("خالد") == "خالد"
    assert m.qstring_to_stdstring("🌎") == "🌎"

    assert m.qstring_to_int("2") == 2
    assert m.int_to_qstring(2) == "2"

    emoji = m.create_qstring_with_emoji()

    assert emoji.encode("utf-16be", "surrogatepass") == b"\xd8\x3d\xde\x00"
    assert m.consume_qstring_with_emoji(emoji) == 2

    assert m.consume_qstring_with_emoji("🌎") == 2


def test_qstringlist():
    assert m.qstringlist_join([""], "--") == ""
    assert m.qstringlist_join(["a", "b"], "") == "ab"
    assert m.qstringlist_join(["x", "y"], ";") == "x;y"

    assert m.qstringlist_at(["x", "y"], 0) == "x"
    assert m.qstringlist_at(["x", "y"], 1) == "y"


def test_qmap():
    assert m.qmap_to_length({"t1": "abc", "t2": "o", "t3": ""}) == {
        "t1": 3,
        "t2": 1,
        "t3": 0,
    }


def test_qdatetime():
    assert m.datetime_from_string("2022-03-01") == QDateTime(2022, 3, 1, 0, 0)

    date = QDateTime(1995, 5, 20, 0, 0)
    assert (
        m.datetime_from_string(
            date.toString(Qt.DateFormat.TextDate), Qt.DateFormat.TextDate
        )
        == date
    )

    assert m.datetime_to_string(date) == "1995-05-20T00:00:00"
    assert m.datetime_to_string(date, Qt.DateFormat.TextDate) == date.toString(
        Qt.DateFormat.TextDate
    )


def test_qvariant():
    # Python -> C++

    assert m.qvariant_from_none(None) == (True, False)

    assert m.qvariant_from_int(-52) == (True, -52)
    assert m.qvariant_from_int(0) == (True, 0)
    assert m.qvariant_from_int(33) == (True, 33)

    assert m.qvariant_from_bool(True) == (True, True)
    assert m.qvariant_from_bool(False) == (True, False)

    assert m.qvariant_from_str("a string") == (True, "a string")

    assert m.qvariant_from_list([]) == (True, [])
    assert m.qvariant_from_list([1, "hello", False]) == (True, [1, "hello", False])

    assert m.qvariant_from_map({"a": 33, "b": False, "c": ["a", "b"]}) == (
        True,
        {"a": 33, "b": False, "c": ["a", "b"]},
    )

    # C++ -> Python (see .cpp file for the value)

    assert m.qvariant_none() is None
    assert m.qvariant_int() == 42
    assert m.qvariant_bool() is True
    assert m.qvariant_str() == "hello world"

    assert m.qvariant_map() == {"baz": "world hello", "bar": 42, "moo": True}

    assert m.qvariant_list() == [
        33,
        [4, "foo"],
        False,
        "hello",
        None,
        {"bar": 42, "moo": [44, True]},
        45,
    ]


def test_qflags():
    v0, v1, v2 = m.SimpleEnum.Value0, m.SimpleEnum.Value1, m.SimpleEnum.Value2

    assert m.qflags_explode(v0 | v1) == (0x3, True, True, False)
    assert m.qflags_explode(v0 | v2) == (0x5, True, False, True)
    assert m.qflags_explode(0) == (0, False, False, False)

    assert not (m.qflags_create(False, False, False) & v0)
    assert not (m.qflags_create(False, False, False) & v1)
    assert not (m.qflags_create(False, False, False) & v2)

    assert m.qflags_create(True, False, False) & v0
    assert m.qflags_create(True, True, False) & v0
    assert m.qflags_create(True, True, False) & v1
    assert not (m.qflags_create(True, True, False) & v2)

    assert m.qflags_create(True, False, False) | v0 == v0
    assert m.qflags_create(True, False, False) | v0 | v2 == v0 | v2
