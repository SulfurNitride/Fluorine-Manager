#ifndef PYTHON_PYBIND11_QT_DETAILS_QLIST_HPP
#define PYTHON_PYBIND11_QT_DETAILS_QLIST_HPP

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace pybind11::detail::qt {

    // helper class for QList to construct from any proper iterable
    //
    template <typename Type, typename Value>
    struct qlist_caster {
        using value_conv = make_caster<Value>;

        bool load(handle src, bool convert)
        {
            if (!isinstance<iterable>(src) || isinstance<bytes>(src) ||
                isinstance<str>(src)) {
                return false;
            }
            auto s = reinterpret_borrow<iterable>(src);
            value.clear();

            if (isinstance<sequence>(src)) {
                value.reserve(s.cast<sequence>().size());
            }
            for (auto it : s) {
                value_conv conv;
                if (!conv.load(it, convert)) {
                    return false;
                }
                value.push_back(cast_op<Value&&>(std::move(conv)));
            }
            return true;
        }

        template <typename T>
        static handle cast(T&& src, return_value_policy policy, handle parent)
        {
            return list_caster<QList<Value>, Value>{}.cast(std::forward<T>(src), policy,
                                                           parent);
        }

        // we type these as "Sequence" even if these can be constructed from Iterable,
        // otherwise the return type will be typed as "Iterable" which is problematic
        PYBIND11_TYPE_CASTER(Type, const_name("Sequence[") + value_conv::name +
                                       const_name("]"));
    };

}  // namespace pybind11::detail::qt

#endif
