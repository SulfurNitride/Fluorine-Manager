#ifndef PYTHON_PYBIND11_QT_HOLDER_HPP
#define PYTHON_PYBIND11_QT_HOLDER_HPP

#include <QObject>

#include <pybind11/pybind11.h>

namespace pybind11::detail::qt {

    class qobject_holder_impl : public QObject {
        object p_;

    public:
        /**
         * @brief Construct a new qobject holder linked to the given QObject and
         * maintaining the given python object alive.
         *
         * @param p Parent of this holder.
         * @param o Python object to keep alive.
         */
        qobject_holder_impl(QObject* p, object o) : p_{o} { setParent(p); }

        template <class U>
        qobject_holder_impl(U* p)
            : qobject_holder_impl{p, reinterpret_borrow<object>(cast(p))}
        {
        }

        ~qobject_holder_impl()
        {
            gil_scoped_acquire s;
            p_ = object();
        }
    };

}  // namespace pybind11::detail::qt

namespace pybind11::qt {

    template <class Type>
    class qobject_holder {
        using type = Type;

        type* qobj_;

    public:
        qobject_holder(type* qobj) : qobj_{qobj}
        {
            new detail::qt::qobject_holder_impl(qobj_);
        }

        type* get() { return qobj_; }
    };

}  // namespace pybind11::qt

PYBIND11_DECLARE_HOLDER_TYPE(T, ::pybind11::qt::qobject_holder<T>)

#endif
