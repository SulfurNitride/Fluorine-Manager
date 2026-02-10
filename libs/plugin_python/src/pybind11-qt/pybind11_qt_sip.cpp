#include "pybind11_qt/details/pybind11_qt_sip.h"

#include <stdexcept>

#include <QString>

#include <sip.h>

namespace py = pybind11;

namespace pybind11::detail::qt {

    const sipAPIDef* sipAPI()
    {
        std::string exception;
        static const sipAPIDef* sipApi = nullptr;
        if (sipApi == nullptr) {
            PyImport_ImportModule("PyQt6.sip");

            {
                auto errorObj = PyErr_Occurred();
                if (errorObj != NULL) {
                    PyObject *type, *value, *traceback;
                    PyErr_Fetch(&type, &value, &traceback);
                    PyErr_NormalizeException(&type, &value, &traceback);
                    if (traceback != NULL) {
                        py::handle h_type(type);
                        py::handle h_val(value);
                        py::handle h_tb(traceback);
                        py::object tb(py::module_::import("traceback"));
                        py::object fmt_exp(tb.attr("format_exception"));
                        py::object exp_list(fmt_exp(h_type, h_val, h_tb));
                        py::object exp_str(py::str("\n").attr("join")(exp_list));
                        exception = exp_str.cast<std::string>();
                    }
                    PyErr_Restore(type, value, traceback);
                    throw std::runtime_error{"Failed to load SIP API: " + exception};
                }
            }

            sipApi = (const sipAPIDef*)PyCapsule_Import("PyQt6.sip._C_API", 0);
            if (sipApi == NULL) {
                auto errorObj = PyErr_Occurred();
                if (errorObj != NULL) {
                    PyObject *type, *value, *traceback;
                    PyErr_Fetch(&type, &value, &traceback);
                    PyErr_NormalizeException(&type, &value, &traceback);
                    if (traceback != NULL) {
                        py::handle h_type(type);
                        py::handle h_val(value);
                        py::handle h_tb(traceback);
                        py::object tb(py::module_::import("traceback"));
                        py::object fmt_exp(tb.attr("format_exception"));
                        py::object exp_list(fmt_exp(h_type, h_val, h_tb));
                        py::object exp_str(py::str("\n").attr("join")(exp_list));
                        exception = exp_str.cast<std::string>();
                    }
                    PyErr_Restore(type, value, traceback);
                }
                throw std::runtime_error{"Failed to load SIP API: " + exception};
            }
        }
        return sipApi;
    }

    namespace sip {
        const sipTypeDef* api_find_type(const char* type)
        {
            return sipAPI()->api_find_type(type);
        }

        int api_can_convert_to_type(PyObject* pyObj, const sipTypeDef* td, int flags)
        {
            return sipAPI()->api_can_convert_to_type(pyObj, td, flags);
        }

        void api_transfer_to(PyObject* self, PyObject* owner)
        {
            sipAPI()->api_transfer_to(self, owner);
        }

        void api_transfer_back(PyObject* self)
        {
            sipAPI()->api_transfer_back(self);
        }

        PyObject* api_convert_from_type(void* cpp, const sipTypeDef* td, PyObject*)
        {
            return sipAPI()->api_convert_from_type(cpp, td, 0);
        }

        void* extract_data(PyObject* ptr)
        {
            if (PyObject_TypeCheck(ptr, sipAPI()->api_simplewrapper_type)) {
                return reinterpret_cast<sipSimpleWrapper*>(ptr)->data;
            }
            else if (PyObject_TypeCheck(ptr, sipAPI()->api_wrapper_type)) {
                return reinterpret_cast<sipWrapper*>(ptr)->super.data;
            }
            return nullptr;
        }

    }  // namespace sip

}  // namespace pybind11::detail::qt
