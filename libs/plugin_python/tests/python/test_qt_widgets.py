from PyQt6.QtWidgets import QWidget

import mobase_tests.qt_widgets as m


class PyWidget(QWidget):
    def heightForWidth(self, a0: int) -> int:
        return a0 * 3 + 4


class PyCustomWidget(m.CustomWidget):
    def __init__(self, name: str):
        super().__init__(name)

    def heightForWidth(self, value: int) -> int:
        return value * 6 - 5


def test_qt_widget():
    # own cpp
    w = m.make_widget_own_cpp("w1")
    assert m.is_alive("w1")
    assert m.is_owned_cpp("w1")

    del w
    assert m.is_alive("w1")

    # own py
    w = m.make_widget_own_py("w2")
    assert m.is_alive("w2")
    assert not m.is_owned_cpp("w2")

    del w
    assert not m.is_alive("w2")

    # transfer to C++
    w = PyWidget()
    m.send_to_cpp("w3", w)

    # delete the reference w - this should NOT delete the underlying object since it
    # was transferred to C++
    del w
    assert m.is_alive("w3")
    assert m.is_owned_cpp("w3")

    # if the Python object is dead (BAD!), this will crash horrible
    assert m.heightForWidth("w3", 4) == 4 * 3 + 4

    # CustomWidget as a qholder, so the construction itself transfers the ownership
    # to C++
    w = PyCustomWidget("w4")
    w.set_parent_cpp()
    assert m.is_alive("w4")
    assert m.heightForWidth("w4", 7) == 6 * 7 - 5
    assert w.heightForWidth(7) == 6 * 7 - 5

    # can call function not defined and not bound through the delegate
    assert not w.hasHeightForWidth()

    del w
    assert m.is_alive("w4")
    assert m.heightForWidth("w4", 7) == 6 * 7 - 5
