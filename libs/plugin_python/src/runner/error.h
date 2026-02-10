#ifndef ERROR_H
#define ERROR_H

#include <format>

#include <QString>

#include <pybind11/pybind11.h>

#include <uibase/utility.h>

namespace pyexcept {

    /**
     * @brief Exception to throw when a python implementation does not implement
     *     a pure virtual function.
     */
    class MissingImplementation : public MOBase::Exception {
    public:
        MissingImplementation(std::string const& className,
                              std::string const& methodName)
            : Exception(QString::fromStdString(
                  std::format("Python class implementing \"{}\" has no "
                              "implementation of method \"{}\".",
                              className, methodName)))
        {
        }
    };

    /**
     * @brief Exception to throw when a python error occurs.
     */
    class PythonError : public MOBase::Exception {
    public:
        /**
         * @brief Create a new PythonError, fetching the error message from
         * python. If the message cannot be retrieved, `defaultErrorMessage()`
         * is used instead.
         */
        PythonError(pybind11::error_already_set const& ex) : Exception(ex.what()) {}

        /**
         * @brief Create a new PythonError with the given message.
         *
         * @param message Message for the exception.
         */
        PythonError(QString message) : Exception(message) {}
    };

    /**
     * @brief Exception to throw when an unknown error occured. This is
     * typically thrown from a catch(...) block.
     */
    class UnknownException : public MOBase::Exception {
    public:
        /**
         * @brief Create a new UnknownException with the default message.
         *
         * @see defaultErrorMessage
         */
        UnknownException() : Exception(defaultErrorMessage()) {}

        /**
         * @brief Create a new UnknownException with the given message.
         *
         * @param message Message for the exception.
         */
        UnknownException(QString message) : Exception(message) {}

    protected:
        /**
         *
         */
        static QString defaultErrorMessage()
        {
            return QObject::tr("An unknown exception was thrown in python code.");
        }
    };

}  // namespace pyexcept

#endif  // ERROR_H
