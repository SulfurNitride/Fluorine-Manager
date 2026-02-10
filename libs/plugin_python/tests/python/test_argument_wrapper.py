import pytest

import mobase_tests.argument_wrapper as m


def test_argument_wrapper_fn1():
    assert m.fn1_raw("hello") == "hello-hello"

    with pytest.raises(TypeError):
        m.fn1_raw(1)  # pyright: ignore[reportArgumentType]

    assert m.fn1_wrap("hello") == "hello-hello"
    assert m.fn1_wrap(32) == "32-32"

    assert m.fn1_wrap_0("world") == "world-world"
    assert m.fn1_wrap_0(45) == "45-45"


def test_argument_wrapper_fn2():
    assert m.fn2_raw(33) == 66

    with pytest.raises(TypeError):
        m.fn2_raw("12")  # pyright: ignore[reportArgumentType]

    assert m.fn2_wrap("15") == 30
    assert m.fn2_wrap(32) == 64

    assert m.fn2_wrap_0("-15") == -30
    assert m.fn2_wrap_0(45) == 90


def test_argument_wrapper_fn3():
    assert m.fn3_raw(33, [], "hello") == "hello-33"
    assert m.fn3_raw(33, [1, 2], "hello") == "hello-35"

    with pytest.raises(TypeError):
        m.fn3_raw("12", [], "hello")  # pyright: ignore[reportArgumentType]

    with pytest.raises(TypeError):
        m.fn3_raw(36, [], 136)  # pyright: ignore[reportArgumentType]

    assert m.fn3_wrap(14, [1, 2], "world") == "world-16"
    assert m.fn3_wrap("15", [0], "woot") == "woot-16"
    assert m.fn3_wrap(17, [], 33) == "33-17"
    assert m.fn3_wrap("15", [], 44) == "44-15"

    assert m.fn3_wrap_0_2(14, [1, 2], "world") == "world-16"
    assert m.fn3_wrap_0_2("15", [0], "woot") == "woot-16"
    assert m.fn3_wrap_0_2(17, [], 33) == "33-17"
    assert m.fn3_wrap_0_2("15", [], 44) == "44-15"

    assert m.fn3_wrap_0(14, [1, 2], "world") == "world-16"
    assert m.fn3_wrap_0("15", [], "w00t") == "w00t-15"

    with pytest.raises(TypeError):
        m.fn3_wrap_0(14, [], 12)  # pyright: ignore[reportArgumentType]

    assert m.fn3_wrap_2(14, [1, 2], "world") == "world-16"
    assert m.fn3_wrap_2(15, [], 18) == "18-15"

    with pytest.raises(TypeError):
        m.fn3_wrap_2("14", [], 12)  # pyright: ignore[reportArgumentType]
