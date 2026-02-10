#include "pybind11_qt/pybind11_qt.h"

#include <pybind11/pybind11.h>

#include <QMap>
#include <QWidget>

#include "MockOrganizer.h"

namespace py = pybind11;
using namespace pybind11::literals;
using ::testing::NiceMock;

PYBIND11_MODULE(organizer, m)
{
    using ::testing::_;
    using ::testing::Eq;
    using ::testing::Return;

    m.def("organizer", []() -> IOrganizer* {
        MockOrganizer* mock = new NiceMock<MockOrganizer>();
        ON_CALL(*mock, profileName).WillByDefault([&mock]() {
            return "profile";
        });

        const auto handle = (HANDLE)std::uintptr_t{4654};
        ON_CALL(*mock, startApplication)
            .WillByDefault([handle](const auto& name, auto&&... args) {
                return name == "valid.exe" ? handle : INVALID_HANDLE_VALUE;
            });
        ON_CALL(*mock, waitForApplication)
            .WillByDefault([&mock, original_handle = handle](HANDLE handle, bool,
                                                             LPDWORD exitCode) {
                if (handle == original_handle) {
                    *exitCode = 0;
                    return true;
                }
                else {
                    *exitCode = static_cast<DWORD>(-1);
                    return false;
                }
            });

        return mock;
    });
}
