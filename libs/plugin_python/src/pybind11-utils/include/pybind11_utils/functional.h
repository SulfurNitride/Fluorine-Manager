#ifndef PYTHON_PYBIND11_FUNCTIONAL_H
#define PYTHON_PYBIND11_FUNCTIONAL_H

#include <pybind11/pybind11.h>

namespace mo2::python::detail {

    // check if the given function is valid for a C++ function with the
    // given arity
    //
    bool has_compatible_arity(pybind11::function handle, std::size_t arity);

}  // namespace mo2::python::detail

namespace pybind11::detail {

    // custom type_caster for std::function<>
    //
    // most of this is from pybind11 except that we also check arity of the function to
    // allow overloaded function based on arity of argument
    //
    template <typename Return, typename... Args>
    struct type_caster<std::function<Return(Args...)>> {
        using type = std::function<Return(Args...)>;
        using retval_type =
            conditional_t<std::is_same<Return, void>::value, void_type, Return>;
        using function_type = Return (*)(Args...);

    public:
        bool load(handle src, bool convert)
        {
            if (src.is_none()) {
                // Defer accepting None to other overloads (if we aren't in convert
                // mode):
                if (!convert) {
                    return false;
                }
                return true;
            }

            if (!isinstance<function>(src)) {
                return false;
            }

            auto func = reinterpret_borrow<function>(src);

            /*
               When passing a C++ function as an argument to another C++
               function via Python, every function call would normally involve
               a full C++ -> Python -> C++ roundtrip, which can be prohibitive.
               Here, we try to at least detect the case where the function is
               stateless (i.e. function pointer or lambda function without
               captured variables), in which case the roundtrip can be avoided.
             */
            if (auto cfunc = func.cpp_function()) {
                auto* cfunc_self = PyCFunction_GET_SELF(cfunc.ptr());
                if (isinstance<capsule>(cfunc_self)) {
                    auto c    = reinterpret_borrow<capsule>(cfunc_self);
                    auto* rec = (function_record*)c;

                    while (rec != nullptr) {
                        if (rec->is_stateless &&
                            same_type(typeid(function_type),
                                      *reinterpret_cast<const std::type_info*>(
                                          rec->data[1]))) {
                            struct capture {
                                function_type f;
                            };
                            value = ((capture*)&rec->data)->f;
                            return true;
                        }
                        rec = rec->next;
                    }
                }
                // PYPY segfaults here when passing builtin function like sum.
                // Raising an fail exception here works to prevent the segfault, but
                // only on gcc. See PR #1413 for full details
            }

            // !MO2! - check arity

            if (!mo2::python::detail::has_compatible_arity(func, sizeof...(Args))) {
                return false;
            }

            // !MO2! - everything below is copy/paste from pybind11

            // ensure GIL is held during functor destruction
            struct func_handle {
                function f;
#if !(defined(_MSC_VER) && _MSC_VER == 1916 && defined(PYBIND11_CPP17))
                // This triggers a syntax error under very special conditions (very
                // weird indeed).
                explicit
#endif
                    func_handle(function&& f_) noexcept
                    : f(std::move(f_))
                {
                }
                func_handle(const func_handle& f_) { operator=(f_); }
                func_handle& operator=(const func_handle& f_)
                {
                    gil_scoped_acquire acq;
                    f = f_.f;
                    return *this;
                }
                ~func_handle()
                {
                    gil_scoped_acquire acq;
                    function kill_f(std::move(f));
                }
            };

            // to emulate 'move initialization capture' in C++11
            struct func_wrapper {
                func_handle hfunc;
                explicit func_wrapper(func_handle&& hf) noexcept : hfunc(std::move(hf))
                {
                }
                Return operator()(Args... args) const
                {
                    gil_scoped_acquire acq;
                    object retval(hfunc.f(std::forward<Args>(args)...));
                    return retval.template cast<Return>();
                }
            };

            value = func_wrapper(func_handle(std::move(func)));
            return true;
        }

        template <typename Func>
        static handle cast(Func&& f_, return_value_policy policy, handle /* parent */)
        {
            if (!f_) {
                return none().inc_ref();
            }

            auto result = f_.template target<function_type>();
            if (result) {
                return cpp_function(*result, policy).release();
            }
            return cpp_function(std::forward<Func>(f_), policy).release();
        }

        PYBIND11_TYPE_CASTER(type, const_name("Callable[[") +
                                       concat(make_caster<Args>::name...) +
                                       const_name("], ") +
                                       make_caster<retval_type>::name +
                                       const_name("]"));
    };

}  // namespace pybind11::detail

#endif
