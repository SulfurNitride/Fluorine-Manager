#ifndef STEAMLAUNCHOPTIONS_H
#define STEAMLAUNCHOPTIONS_H

#include "steamcef.h"

#include <QString>

// Reads and writes a game's launch options through Steam's own UI scripting
// (SteamClient.Apps), so Steam keeps its config consistent. Widget-free.
namespace SteamLaunchOptions
{

// Both throw std::invalid_argument unless appId is all digits.
QString readExpression(const QString& appId);
QString writeExpression(const QString& appId, const QString& options);

// details.strLaunchOptions, or "" if unset.
QString read(const SteamCef::Client& client, const QString& appId);

// Throws SteamCef::Error(ScriptFailed) if Steam doesn't confirm the write.
void write(const SteamCef::Client& client, const QString& appId,
           const QString& options);

}  // namespace SteamLaunchOptions

#endif
