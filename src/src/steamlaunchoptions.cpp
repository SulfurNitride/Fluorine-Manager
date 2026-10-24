#include "steamlaunchoptions.h"

#include <QJsonObject>
#include <stdexcept>

namespace SteamLaunchOptions
{

namespace
{

  // The id is spliced into JS unquoted, so anything but digits is refused.
  QString checkedAppId(const QString& appId)
  {
    if (appId.isEmpty())
      throw std::invalid_argument("empty Steam app id");
    for (const QChar c : appId) {
      if (c < QLatin1Char('0') || c > QLatin1Char('9'))
        throw std::invalid_argument("Steam app id must be digits");
    }
    return appId;
  }

}  // namespace

QString readExpression(const QString& appId)
{
  return QStringLiteral(R"JS(new Promise((resolve,reject)=>{
    let h; const t=setTimeout(()=>{h?.unregister();reject(new Error('timeout'));},5000);
    h=SteamClient.Apps.RegisterForAppDetails(%1,d=>{
      clearTimeout(t);setTimeout(()=>h?.unregister(),0);
      resolve({launchOptions:d.strLaunchOptions || ''});
    });
  }))JS")
      .arg(checkedAppId(appId));
}

QString writeExpression(const QString& appId, const QString& options)
{
  return QStringLiteral("(()=>{SteamClient.Apps.SetAppLaunchOptions(%1, "
                        "%2);return {ok:true};})()")
      .arg(checkedAppId(appId), SteamCef::jsString(options));
}

QString read(const SteamCef::Client& client, const QString& appId)
{
  return client.evaluate(readExpression(appId))
      .toObject()
      .value("launchOptions")
      .toString();
}

void write(const SteamCef::Client& client, const QString& appId, const QString& options)
{
  const auto reply = client.evaluate(writeExpression(appId, options));
  if (!reply.toObject().value("ok").toBool())
    throw SteamCef::Error(
        SteamCef::Error::Kind::ScriptFailed,
        QStringLiteral("Steam did not confirm the launch option change."));
}

}  // namespace SteamLaunchOptions
