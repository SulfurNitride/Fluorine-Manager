#include "pythonutils.h"

#include <filesystem>
#include <set>
#include <sstream>

#include <pybind11/eval.h>
#include <pybind11/pybind11.h>

#include <uibase/log.h>

namespace py = pybind11;

namespace mo2::python {

    class PrintWrapper {
        MOBase::log::Levels level_;
        std::stringstream buffer_;

    public:
        PrintWrapper(MOBase::log::Levels level) : level_{level} {}

        void write(std::string_view message)
        {
            buffer_ << message;
            if (buffer_.tellp() != 0 && buffer_.str().back() == '\n') {
                const auto full_message = buffer_.str();
                MOBase::log::log(level_, "{}",
                                 full_message.substr(0, full_message.length() - 1));
                buffer_ = std::stringstream{};
            }
        }
    };

    /**
     * @brief Construct a dynamic Python type.
     *
     */
    template <class... Args>
    pybind11::object make_python_type(std::string_view name,
                                      pybind11::tuple base_classes, Args&&... args)
    {
        // this is ugly but that's how it's done in C Python
        auto type = py::reinterpret_borrow<py::object>((PyObject*)&PyType_Type);

        // create the python class
        return type(name, base_classes, py::dict(std::forward<Args>(args)...));
    }

    void configure_python_stream()
    {
        // create the "MO2Handler" python class
        auto printWrapper = make_python_type(
            "MO2PrintWrapper", py::make_tuple(),
            py::arg("write") = py::cpp_function([](std::string_view message) {
                static PrintWrapper wrapper(MOBase::log::Debug);
                wrapper.write(message);
            }),
            py::arg("flush") = py::cpp_function([] {}));
        auto errorWrapper = make_python_type(
            "MO2ErrorWrapper", py::make_tuple(),
            py::arg("write") = py::cpp_function([](std::string_view message) {
                static PrintWrapper wrapper(MOBase::log::Error);
                wrapper.write(message);
            }),
            py::arg("flush") = py::cpp_function([] {}));
        py::module_ sys    = py::module_::import("sys");
        sys.attr("stdout") = printWrapper();
        sys.attr("stderr") = errorWrapper();

        // this is required to handle exception in Python code OUTSIDE of pybind11 call,
        // typically on Qt classes with methods overridden on the Python side
        //
        // without this, the application will crash instead of properly handling the
        // exception as it would do with a py::error_already_set{}
        //
        // IMPORTANT: sys.attr("excepthook") = sys.attr("__excepthook__") DOES NOT WORK,
        // and I have no clue why since the attribute does not seem to get updated (at
        // least a print does not show it)
        //
        sys.attr("excepthook") =
            py::eval("lambda x, y, z: sys.__excepthook__(x, y, z)");
    }

    // Small structure to hold the levels - There are copy paste from
    // my Python version and I assume these will not change soon:
    struct PyLogLevel {
        static constexpr int CRITICAL = 50;
        static constexpr int ERROR    = 40;
        static constexpr int WARNING  = 30;
        static constexpr int INFO     = 20;
        static constexpr int DEBUG    = 10;
    };

    // This is the function we are going to use as our Handler .emit
    // method.
    void emit_function(py::object record)
    {

        // There are other parameters that could be used, but this is minimal
        // for now (filename, line number, etc.).
        const int level        = record.attr("levelno").cast<int>();
        const std::wstring msg = py::str(record.attr("msg")).cast<std::wstring>();

        switch (level) {
        case PyLogLevel::CRITICAL:
        case PyLogLevel::ERROR:
            MOBase::log::error("{}", msg);
            break;
        case PyLogLevel::WARNING:
            MOBase::log::warn("{}", msg);
            break;
        case PyLogLevel::INFO:
            MOBase::log::info("{}", msg);
            break;
        case PyLogLevel::DEBUG:
        default:  // There is a "NOTSET" level in theory:
            MOBase::log::debug("{}", msg);
            break;
        }
    };

    void configure_python_logging(py::module_ mobase)
    {
        // most of this is dealing with actual Python objects since it is not
        // possible to derive from logging.Handler in C++ using pybind11,
        // and since a lot of this would require extra register only for this.

        // see also
        // https://github.com/pybind/pybind11/issues/1193#issuecomment-429451094

        // retrieve the logging module and the Handler class.
        auto logging = py::module_::import("logging");
        auto Handler = logging.attr("Handler");

        // create the "MO2Handler" python class
        auto MO2Handler =
            make_python_type("LogHandler", py::make_tuple(Handler),
                             py::arg("emit") = py::cpp_function(emit_function));

        // create the default logger
        auto handler = MO2Handler();
        handler.attr("setLevel")(PyLogLevel::DEBUG);
        auto logger = logging.attr("getLogger")(py::object(mobase.attr("__name__")));
        logger.attr("setLevel")(PyLogLevel::DEBUG);

        // set mobase attributes
        mobase.attr("LogHandler") = MO2Handler;
        mobase.attr("logger")     = logger;

        logging.attr("root").attr("setLevel")(PyLogLevel::DEBUG);
        logging.attr("root").attr("addHandler")(handler);
    }

}  // namespace mo2::python
