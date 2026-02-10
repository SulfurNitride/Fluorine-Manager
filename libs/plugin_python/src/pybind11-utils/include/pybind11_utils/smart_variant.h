#ifndef PYTHON_PYBIND11_UTILS_SMART_VARIANT_H
#define PYTHON_PYBIND11_UTILS_SMART_VARIANT_H

#include <variant>

namespace mo2::python {

    namespace detail {

        // simple template class that should be specialized to expose proper fromXXX
        // methods
        //
        template <class T>
        struct smart_variant_converter {
            template <class U>
            static T from(U&& u)
            {
                return T{std::forward<U>(u)};
            }
        };

    }  // namespace detail

    // a smart_variant is a std::variant that can be automatically converted to any of
    // its type via custom operator T()
    //
    // user should specialize detail::smart_variant_converter to provide proper
    // conversions
    //
    template <class... Args>
    struct smart_variant : std::variant<Args...> {
        using std::variant<Args...>::variant;

        template <class T, std::enable_if_t<
                               std::disjunction_v<std::is_same<T, Args>...>, int> = 0>
        operator T() const
        {
            return std::visit(
                [](auto const& t) -> T {
                    if constexpr (std::is_same_v<std::decay_t<decltype(t)>, T>) {
                        return t;
                    }
                    else {
                        return detail::smart_variant_converter<T>::from(t);
                    }
                },
                *this);
        }
    };

}  // namespace mo2::python

#endif
