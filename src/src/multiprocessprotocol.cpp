#include "multiprocessprotocol.h"

#include <QStringDecoder>
#include <QtEndian>

#include <cstring>
#include <utility>

namespace multiprocess_ipc {
namespace {

constexpr char Magic[] = {'F', 'M', 'I', 'P'};
constexpr quint8 ProtocolVersion = 1;
constexpr quint8 MessageType = 1;
constexpr char Accepted[] = {'F', 'M', 'O', 'K', 1};
constexpr char Rejected[] = {'F', 'M', 'N', 'O', 1};

} // namespace

QByteArray encodeMessage(QStringView message) {
  const QByteArray payload = message.toUtf8();
  if (payload.isEmpty() || payload.size() > MaximumPayload ||
      payload.contains('\0')) {
    return {};
  }

  QByteArray frame;
  frame.reserve(HeaderSize + payload.size());
  frame.append(Magic, sizeof(Magic));
  frame.append(static_cast<char>(ProtocolVersion));
  frame.append(static_cast<char>(MessageType));

  const quint32 length = qToBigEndian(static_cast<quint32>(payload.size()));
  frame.append(reinterpret_cast<const char *>(&length), sizeof(length));
  frame.append(payload);
  return frame;
}

DecodeResult decodeMessage(const QByteArray &bytes) {
  if (bytes.size() < HeaderSize) {
    return {};
  }

  if (std::memcmp(bytes.constData(), Magic, sizeof(Magic)) != 0 ||
      static_cast<quint8>(bytes[4]) != ProtocolVersion ||
      static_cast<quint8>(bytes[5]) != MessageType) {
    return {DecodeStatus::Invalid, {}};
  }

  quint32 encodedLength = 0;
  std::memcpy(&encodedLength, bytes.constData() + 6, sizeof(encodedLength));
  const qsizetype payloadLength = qFromBigEndian(encodedLength);
  if (payloadLength == 0 || payloadLength > MaximumPayload) {
    return {DecodeStatus::Invalid, {}};
  }

  const qsizetype frameLength = HeaderSize + payloadLength;
  if (bytes.size() < frameLength) {
    return {};
  }
  if (bytes.size() != frameLength) {
    return {DecodeStatus::Invalid, {}};
  }

  const QByteArrayView payload(bytes.constData() + HeaderSize, payloadLength);
  if (payload.contains('\0')) {
    return {DecodeStatus::Invalid, {}};
  }

  QStringDecoder decoder(QStringDecoder::Utf8);
  QString message = decoder.decode(payload);
  if (decoder.hasError() || message.isEmpty()) {
    return {DecodeStatus::Invalid, {}};
  }

  return {DecodeStatus::Complete, std::move(message)};
}

QByteArray acceptedReply() { return QByteArray(Accepted, sizeof(Accepted)); }

QByteArray rejectedReply() { return QByteArray(Rejected, sizeof(Rejected)); }

bool isAcceptedReply(const QByteArray &bytes) {
  return bytes.size() == static_cast<qsizetype>(sizeof(Accepted)) &&
         std::memcmp(bytes.constData(), Accepted, sizeof(Accepted)) == 0;
}

} // namespace multiprocess_ipc
