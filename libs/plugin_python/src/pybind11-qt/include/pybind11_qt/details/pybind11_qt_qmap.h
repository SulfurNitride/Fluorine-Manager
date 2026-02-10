#ifndef PYTHON_PYBIND11_QT_DETAILS_QMAP_HPP
#define PYTHON_PYBIND11_QT_DETAILS_QMAP_HPP

#include <pybind11/pybind11.h>

namespace pybind11::detail::qt {

    // helper class for QMap because QMap do not follow the standard std:: maps
    // interface, for other containers, the pybind11 built-in xxx_caster works
    //
    // this code is basically a copy/paste from the pybind11 stl stuff with
    // minor modifications
    //
    template <typename Type, typename Key, typename Value>
    struct qmap_caster {
        using key_conv   = make_caster<Key>;
        using value_conv = make_caster<Value>;

        bool load(handle src, bool convert)
        {
            if (!isinstance<dict>(src)) {
                return false;
            }
            auto d = reinterpret_borrow<dict>(src);
            value.clear();
            for (auto it : d) {
                key_conv kconv;
                value_conv vconv;
                if (!kconv.load(it.first.ptr(), convert) ||
                    !vconv.load(it.second.ptr(), convert)) {
                    return false;
                }
                value[cast_op<Key&&>(std::move(kconv))] =
                    cast_op<Value&&>(std::move(vconv));
            }
            return true;
        }

        template <typename T>
        static handle cast(T&& src, return_value_policy policy, handle parent)
        {
            dict d;
            return_value_policy policy_key   = policy;
            return_value_policy policy_value = policy;
            if (!std::is_lvalue_reference<T>::value) {
                policy_key = return_value_policy_override<Key>::policy(policy_key);
                policy_value =
                    return_value_policy_override<Value>::policy(policy_value);
            }
            for (auto it = src.begin(); it != src.end(); ++it) {
                auto key = reinterpret_steal<object>(
                    key_conv::cast(forward_like<T>(it.key()), policy_key, parent));
                auto value = reinterpret_steal<object>(value_conv::cast(
                    forward_like<T>(it.value()), policy_value, parent));
                if (!key || !value) {
                    return handle();
                }
                d[key] = value;
            }
            return d.release();
        }

        PYBIND11_TYPE_CASTER(Type, const_name("Dict[") + key_conv::name +
                                       const_name(", ") + value_conv::name +
                                       const_name("]"));
    };

}  // namespace pybind11::detail::qt

#endif
