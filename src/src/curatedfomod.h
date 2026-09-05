#pragma once

#include <QPair>
#include <QString>
#include <QStringList>

// Apply a reviewed set of FOMOD choices to an already extracted archive.
// The destination is expected to be a disposable staging directory.
QPair<bool, QString> installCuratedFomod(const QString& source,
                                        const QString& destination,
                                        const QStringList& selectedPlugins);
