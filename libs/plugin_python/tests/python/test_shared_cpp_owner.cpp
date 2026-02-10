#include "pybind11_utils/shared_cpp_owner.h"

#include <pybind11/pybind11.h>

#include <tuple>
#include <unordered_map>

namespace py = pybind11;
using namespace pybind11::literals;

class Base;
static std::unordered_map<std::string, Base*> bases;

class Base {
    std::string name_;

public:
    Base(std::string const& name) : name_{name} { bases[name] = this; }
    virtual std::string fn() const = 0;
    virtual ~Base() { bases.erase(name_); }
};

MO2_PYBIND11_SHARED_CPP_HOLDER(Base);

class CppBase : public Base {
public:
    using Base::Base;
    std::string fn() const override { return "CppBase::fn()"; }
};

class PyBase : public Base {
public:
    using Base::Base;
    std::string fn() const override { PYBIND11_OVERRIDE_PURE(std::string, Base, fn, ); }
};

PYBIND11_MODULE(shared_cpp_owner, m)
{
    static std::shared_ptr<Base> base_ptr;

    py::class_<Base, PyBase, std::shared_ptr<Base>>(m, "Base")
        .def(py::init<std::string>())
        .def("fn", &Base::fn);

    m.def("is_alive", [](std::string const& name) {
        return bases.find(name) != bases.end();
    });

    m.def("create", [](std::string const& name) -> std::shared_ptr<Base> {
        return std::make_shared<CppBase>(name);
    });
    m.def("create_and_store", [](std::string const& name) {
        base_ptr = std::make_shared<CppBase>(name);
        return base_ptr;
    });
    m.def("store", [](std::shared_ptr<Base> ptr) {
        base_ptr = ptr;
    });
    m.def("clear", []() {
        base_ptr.reset();
    });

    m.def("call_fn", [](std::string const& name) {
        auto it = bases.find(name);
        return it != bases.end() ? it->second->fn() : "";
    });
}
