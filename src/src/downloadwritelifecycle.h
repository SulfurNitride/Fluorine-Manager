#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QPointer>

namespace download_write
{
enum class Status
{
  Unavailable,
  Complete,
  ShortWrite,
};

struct Result
{
  Status status{Status::Unavailable};
  qint64 requested{0};
  qint64 written{0};

  bool complete() const noexcept { return status == Status::Complete; }
};

inline Result drain(QIODevice& source, QIODevice& destination)
{
  const QByteArray data = source.readAll();
  const qint64 requested = data.size();
  const qint64 written = destination.write(data);
  return {written == requested ? Status::Complete : Status::ShortWrite,
          requested, written};
}

template <typename Reply>
struct Identity
{
  unsigned int downloadID;
  QPointer<Reply> reply;
};

template <typename Download>
struct Continuation
{
  Result result;
  Download* download;
};

// A write failure may synchronously run callbacks that erase or replace the
// owning download. Keeping the write and authentication order in one
// production-used operation prevents callers from accidentally resuming with
// the pointer they passed to the writer.
template <typename Download, typename Writer, typename Authenticator>
Continuation<Download> continueAfterWrite(Writer&& writer,
                                          Authenticator&& authenticate)
{
  const Result result = writer();
  return {result, authenticate()};
}

template <typename Download, typename Reply, typename Lookup, typename ReplyAccessor>
Download* reacquire(const Identity<Reply>& identity, Lookup&& lookup,
                    ReplyAccessor&& replyAccessor)
{
  if (identity.reply.isNull()) {
    return nullptr;
  }

  Download* current = lookup(identity.downloadID);
  if (current == nullptr || replyAccessor(*current) != identity.reply.data()) {
    return nullptr;
  }
  return current;
}

template <typename Download, typename Reply, typename Lookup, typename ReplyAccessor>
Download* reacquireSameOrRetired(const Identity<Reply>& identity, Lookup&& lookup,
                                 ReplyAccessor&& replyAccessor)
{
  Download* current = lookup(identity.downloadID);
  if (current == nullptr) {
    return nullptr;
  }

  Reply* currentReply = replyAccessor(*current);
  if (currentReply != nullptr &&
      (identity.reply.isNull() || currentReply != identity.reply.data())) {
    return nullptr;
  }
  return current;
}
}  // namespace download_write
