#pragma once

#include <QPair>
#include <QSettings>
#include <QString>

QString curatedGamePluginId(const QString& gamePlugin);

// Rebase paths authored against the original game directory to an isolated
// Stock Game directory. Only the executable fields that actually contain the
// original game root are changed; instance-local tools remain untouched.
int rebaseCustomExecutableGamePaths(QSettings& ini,
                                    const QString& sourceGamePath,
                                    const QString& stockGamePath);

QPair<bool, QString> bootstrapCuratedInstance(const QString& instancePath,
                                              const QString& gamePlugin,
                                              const QString& gamePath,
                                              const QString& downloadsPath);
