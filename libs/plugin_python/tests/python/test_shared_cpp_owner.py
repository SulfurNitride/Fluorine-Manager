import mobase_tests.shared_cpp_owner as m


class PyBase(m.Base):
    def __init__(self, name: str, value: int):
        super().__init__(name)
        self.value = value

    def fn(self):
        return f"PyBase.fn({self.value})"


def test_shared_cpp_owner_1():
    # create from C++, owned by Python

    # create from C++
    p = m.create("tmp")
    assert m.is_alive("tmp")

    # should delete since it's not owner by C++
    del p
    assert not m.is_alive("tmp")


def test_shared_cpp_owner_2():
    # create from C++, owned by C++ (and Python)

    # create from C++
    p = m.create_and_store("tmp")
    assert m.is_alive("tmp")

    # should not delete since it's owned by both C++ and Python
    del p
    assert m.is_alive("tmp")

    # clear from C++ should free it
    m.clear()
    assert not m.is_alive("tmp")


def test_shared_cpp_owner_3():
    # create from Python, owned by Python

    p = PyBase("foo", 1)
    assert m.is_alive("foo")
    assert m.call_fn("foo") == "PyBase.fn(1)"

    del p
    assert not m.is_alive("foo")


def test_shared_cpp_owner_4():
    # create from Python, owned by C++

    p = PyBase("foo", 2)
    assert m.is_alive("foo")

    # send to C++
    m.store(p)
    assert m.is_alive("foo")
    assert m.call_fn("foo") == "PyBase.fn(2)"

    # delete in Python, should still be alived
    del p
    assert m.is_alive("foo")

    # should still be able to call fn()
    assert m.call_fn("foo") == "PyBase.fn(2)"

    # clear in C++, should kill Python
    m.clear()
    assert not m.is_alive("foo")
