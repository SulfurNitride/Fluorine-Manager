#ifndef PYTHON_PYBIND11_UTILS_SMART_VARIANT_WRAPPER_H
#define PYTHON_PYBIND11_UTILS_SMART_VARIANT_WRAPPER_H

#include <functional>
#include <type_traits>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "smart_variant.h"

namespace mo2::python {

    namespace detail {

        // simple helper class that expose a ::type attribute which is U is I is in Is,
        // V otherwise
        template <class U, class V, std::size_t I, class Is>
        struct wrap_arg;

        template <class U, class V, std::size_t I, std::size_t... Is>
        struct wrap_arg<U, V, I, std::index_sequence<Is...>> {
            using type =
                std::conditional_t<std::disjunction_v<std::bool_constant<I == Is>...>,
                                   U, V>;
        };

        // helper type for wrap_arg
        template <class U, class A, std::size_t I, class Is>
        using wrap_arg_t = typename wrap_arg<U, A, I, Is>::type;

        template <class T, std::size_t... Is, std::size_t... AIs, class Fn, class R,
                  class... Args>
        auto wrap_arguments_impl(std::index_sequence<Is...>, Fn&& fn, R (*)(Args...),
                                 std::index_sequence<AIs...>)
        {
            return [fn = std::forward<Fn>(fn)](
                       wrap_arg_t<T, Args, AIs, std::index_sequence<Is...>>... args) {
                return std::invoke(fn, std::forward<decltype(args)>(args)...);
            };
        }

        template <class T, class... Args, std::size_t... Is>
        auto make_convertible_index_sequence(std::index_sequence<Is...>)
        {
            return std::index_sequence<(std::is_convertible_v<T, Args> ? Is : -1)...>{};
        }

        template <class T, std::size_t... Is, class Fn, class R, class... Args>
        auto wrap_arguments_impl(Fn&& fn, R (*sg)(Args...))
        {
            if constexpr (sizeof...(Is) == 0) {
                return wrap_arguments_impl<T>(
                    make_convertible_index_sequence<T, Args...>(
                        std::make_index_sequence<sizeof...(Args)>{}),
                    std::forward<Fn>(fn), sg,
                    std::make_index_sequence<sizeof...(Args)>{});
            }
            else {
                return wrap_arguments_impl<T>(
                    std::index_sequence<Is...>{}, std::forward<Fn>(fn), sg,
                    std::make_index_sequence<sizeof...(Args)>{});
            }
        }

        template <class T, class Fn, class R, class... Args>
        auto wrap_return_impl(Fn&& fn, R (*)(Args...))
        {
            return [fn = std::forward<Fn>(fn)](Args... args) {
                return T{std::invoke(fn, std::forward<decltype(args)>(args)...)};
            };
        }

        // make_python_function_signature: return a null-pointer with the proper type
        // for the given function

        template <class Fn>
        struct function_signature {
            using type =
                pybind11::detail::function_signature_t<std::remove_reference_t<Fn>>;
        };

        template <class R, class... Args>
        struct function_signature<R (*)(Args...)> {
            using type = R(Args...);
        };

        template <class R, class C, class... Args>
        struct function_signature<R (C::*)(Args...)> {
            using type = R(C*, Args...);
        };

        template <class R, class C, class... Args>
        struct function_signature<R (C::*)(Args...) &> {
            using type = R(C*, Args...);
        };

        template <class R, class C, class... Args>
        struct function_signature<R (C::*)(Args...) const> {
            using type = R(const C*, Args...);
        };

        template <class R, class C, class... Args>
        struct function_signature<R (C::*)(Args...) const&> {
            using type = R(const C*, Args...);
        };

        template <class Fn>
        using function_signature_t = typename function_signature<Fn>::type;

        template <class Type, class... WrappedTypes>
        class wrap_type_caster {
            using variant_type   = std::variant<WrappedTypes...>;
            using variant_caster = pybind11::detail::make_caster<variant_type>;

        public:
            PYBIND11_TYPE_CASTER(Type, variant_caster::name);

            bool load(pybind11::handle src, bool convert)
            {
                variant_caster caster;

                if (!caster.load(src, convert)) {
                    return false;
                }

                value = std::visit(
                    [](auto const& fn) {
                        return Type(fn);
                    },
                    static_cast<variant_type>(caster));
                return true;
            }

            static pybind11::handle cast(const Type& src,
                                         pybind11::return_value_policy policy,
                                         pybind11::handle parent)
            {
                return variant_caster::cast(variant_type(std::in_place_index<0>, src),
                                            policy, parent);
            }
        };

    }  // namespace detail

    // wrap the given function-like object to accept T instead of the specified
    // arguments at the specified positions
    //
    // if the list of positions is empty, replace all arguments that can be converted to
    // T
    //
    template <class T, std::size_t... Is, class Fn>
    auto wrap_arguments(Fn&& fn)
    {
        return detail::wrap_arguments_impl<T, Is...>(
            std::forward<Fn>(fn),
            (mo2::python::detail::function_signature_t<Fn>*)nullptr);
    }

    // wrap the given function-like object to return T instead of the specified type
    //
    template <class T, class Fn>
    auto wrap_return(Fn&& fn)
    {
        return detail::wrap_return_impl<T>(
            std::forward<Fn>(fn),
            (mo2::python::detail::function_signature_t<Fn>*)nullptr);
    }

}  // namespace mo2::python

namespace pybind11::detail {

    template <class... Args>
    struct type_caster<::mo2::python::smart_variant<Args...>>
        : variant_caster<::mo2::python::smart_variant<Args...>> {};

}  // namespace pybind11::detail

#endif
