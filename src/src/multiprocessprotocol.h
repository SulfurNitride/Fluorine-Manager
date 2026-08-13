#ifndef FLUORINE_MULTIPROCESSPROTOCOL_H
#define FLUORINE_MULTIPROCESSPROTOCOL_H

#include <QByteArray>
#include <QString>
#include <QStringView>

namespace multiprocess_ipc {

inline constexpr qsizetype HeaderSize = 10;
inline constexpr qsizetype MaximumPayload = 256 * 1024;

enum class DecodeStatus {
  Incomplete,
  Complete,
  Invalid,
};

struct DecodeResult {
  DecodeStatus status = DecodeStatus::Incomplete;
  QString message;
};

QByteArray encodeMessage(QStringView message);
DecodeResult decodeMessage(const QByteArray &bytes);

QByteArray acceptedReply();
bool isAcceptedReply(const QByteArray &bytes);

} // namespace multiprocess_ipc

#endif // FLUORINE_MULTIPROCESSPROTOCOL_H
