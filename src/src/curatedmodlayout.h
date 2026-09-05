#pragma once

#include <QString>

QString curatedModPayloadRoot(const QString& extractedRoot);
QString curatedRootPayloadRoot(const QString& extractedRoot);
QString curatedNestedFomodArchive(const QString& extractedRoot,
                                  QString* error = nullptr);
bool validateCuratedModLayout(const QString& modRoot, QString* error = nullptr);
