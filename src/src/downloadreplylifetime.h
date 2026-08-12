#pragma once

#include <QPointer>

namespace download_reply
{
// Clear the owner's observable pointer before scheduling deferred deletion so
// a later callback cannot dereference a reply the event loop already deleted.
template <typename Reply>
Reply* retire(QPointer<Reply>& reply) noexcept
{
  Reply* retired = reply.data();
  reply.clear();
  return retired;
}
}  // namespace download_reply
