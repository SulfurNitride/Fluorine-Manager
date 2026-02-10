
#include "wrappers.h"

#include "../pybind11_all.h"

#include <QDir>
#include <QIcon>
#include <QString>
#include <QUrl>

// IOrganizer must be bring here to properly compile the Python bindings of
// plugin requirements
#include <uibase/imoinfo.h>
#include <uibase/isavegame.h>
#include <uibase/isavegameinfowidget.h>
#include <uibase/pluginrequirements.h>

using namespace pybind11::literals;
namespace py = pybind11;
using namespace MOBase;

namespace mo2::python {

    class PyPluginRequirement : public IPluginRequirement {
    public:
        std::optional<Problem> check(IOrganizer* organizer) const override
        {
            PYBIND11_OVERRIDE_PURE(std::optional<Problem>, IPluginRequirement, check,
                                   organizer);
        };
    };

    class PySaveGame : public ISaveGame {
    public:
        QString getFilepath() const override
        {
            PYBIND11_OVERRIDE_PURE(FileWrapper, ISaveGame, getFilepath, );
        }

        QDateTime getCreationTime() const override
        {
            PYBIND11_OVERRIDE_PURE(QDateTime, ISaveGame, getCreationTime, );
        }

        QString getName() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, ISaveGame, getName, );
        }

        QString getSaveGroupIdentifier() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, ISaveGame, getSaveGroupIdentifier, );
        }

        QStringList allFiles() const override
        {
            return toQStringList([&] {
                PYBIND11_OVERRIDE_PURE(QList<FileWrapper>, ISaveGame, allFiles, );
            }());
        }

        ~PySaveGame() { std::cout << "~PySaveGame()" << std::endl; }
    };

    class PySaveGameInfoWidget : public ISaveGameInfoWidget {
    public:
        // Bring the constructor:
        using ISaveGameInfoWidget::ISaveGameInfoWidget;

        void setSave(ISaveGame const& save) override
        {
            PYBIND11_OVERRIDE_PURE(void, ISaveGameInfoWidget, setSave, &save);
        }

        ~PySaveGameInfoWidget() { std::cout << "~PySaveGameInfoWidget()" << std::endl; }
    };

    void add_wrapper_bindings(pybind11::module_ m)
    {
        // ISaveGame - custom type_caster<> for shared_ptr<> to keep the Python object
        // alive when returned from Python (see shared_cpp_owner.h)

        py::class_<ISaveGame, PySaveGame, std::shared_ptr<ISaveGame>>(m, "ISaveGame")
            .def(py::init<>())
            .def("getFilepath", wrap_return_for_filepath(&ISaveGame::getFilepath))
            .def("getCreationTime", &ISaveGame::getCreationTime)
            .def("getName", &ISaveGame::getName)
            .def("getSaveGroupIdentifier", &ISaveGame::getSaveGroupIdentifier)
            .def("allFiles", [](ISaveGame* s) -> QList<FileWrapper> {
                const auto result = s->allFiles();
                return {result.begin(), result.end()};
            });

        // ISaveGameInfoWidget - custom holder to keep the Python object alive alongside
        // the widget

        py::class_<ISaveGameInfoWidget, PySaveGameInfoWidget,
                   py::qt::qobject_holder<ISaveGameInfoWidget>>
            iSaveGameInfoWidget(m, "ISaveGameInfoWidget");
        iSaveGameInfoWidget.def(py::init<QWidget*>(), "parent"_a = (QWidget*)nullptr)
            .def("setSave", &ISaveGameInfoWidget::setSave, "save"_a);
        py::qt::add_qt_delegate<QWidget>(iSaveGameInfoWidget, "_widget");

        // IPluginRequirement - custom type_caster<> for shared_ptr<> to keep the Python
        // object alive when returned from Python (see shared_cpp_owner.h)

        py::class_<IPluginRequirement, std::shared_ptr<IPluginRequirement>,
                   PyPluginRequirement>
            iPluginRequirementClass(m, "IPluginRequirement");

        py::class_<IPluginRequirement::Problem>(iPluginRequirementClass, "Problem")
            .def(py::init<QString, QString>(), "short_description"_a,
                 "long_description"_a = "")
            .def("shortDescription", &IPluginRequirement::Problem::shortDescription)
            .def("longDescription", &IPluginRequirement::Problem::longDescription);

        iPluginRequirementClass.def("check", &IPluginRequirement::check, "organizer"_a);
    }

}  // namespace mo2::python
