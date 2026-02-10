/*
Copyright (C) 2022 Sebastian Herbord & MO2 Team. All rights reserved.

This file is part of python proxy plugin for MO

python proxy plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Python proxy plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with python proxy plugin.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef PROXYPYTHON_H
#define PROXYPYTHON_H

#include <map>
#include <memory>

#include <uibase/iplugindiagnose.h>
#include <uibase/ipluginproxy.h>

#include <pythonrunner.h>

class ProxyPython : public QObject,
                    public MOBase::IPluginProxy,
                    public MOBase::IPluginDiagnose {
    Q_OBJECT
    Q_INTERFACES(MOBase::IPlugin MOBase::IPluginProxy MOBase::IPluginDiagnose)
    Q_PLUGIN_METADATA(IID "org.mo2.ProxyPython")

public:
    ProxyPython();

    virtual bool init(MOBase::IOrganizer* moInfo);
    virtual QString name() const override;
    virtual QString localizedName() const override;
    virtual QString author() const override;
    virtual QString description() const override;
    virtual MOBase::VersionInfo version() const override;
    virtual QList<MOBase::PluginSetting> settings() const override;

    QStringList pluginList(const QDir& pluginPath) const override;
    QList<QObject*> load(const QString& identifier) override;
    void unload(const QString& identifier) override;

public:  // IPluginDiagnose
    virtual std::vector<unsigned int> activeProblems() const override;
    virtual QString shortDescription(unsigned int key) const override;
    virtual QString fullDescription(unsigned int key) const override;
    virtual bool hasGuidedFix(unsigned int key) const override;
    virtual void startGuidedFix(unsigned int key) const override;

private:
    MOBase::IOrganizer* m_MOInfo;
#ifdef _WIN32
    HMODULE m_RunnerLib;
#else
    void* m_RunnerLib;
#endif
    std::unique_ptr<mo2::python::IPythonRunner> m_Runner;

    enum class FailureType : unsigned int {
        NONE           = 0,
        SEMICOLON      = 1,
        DLL_NOT_FOUND  = 2,
        INVALID_DLL    = 3,
        INITIALIZATION = 4
    };

    FailureType m_LoadFailure;
};

#endif  // PROXYPYTHON_H
