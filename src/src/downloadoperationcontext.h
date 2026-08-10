#pragma once

#include "settingswritebarrier.h"

#include <QPointer>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace download_operation
{
// Clear the owner's observable pointer before scheduling deferred deletion.
// Fail-stop phase two may run after the deferred delete has executed, so a raw
// pointer retained in DownloadInfo would otherwise be a use-after-free.
template <typename Reply>
Reply* retireReply(QPointer<Reply>& reply) noexcept
{
  Reply* retired = reply.data();
  reply.clear();
  return retired;
}
}  // namespace download_operation

// Coordinates the two distinct lifetimes of a download operation during a
// terminal rollback fail-stop:
//
//  * mutation leases cover synchronous manager calls and asynchronous slots;
//    phase one waits for those frames before touching their replies/state;
//  * operation leases cover the network/request lifetime after the initiating
//    call returns; phase two aborts those operations and retires their leases.
//
// Admission closure never waits. This is important when fail-stop runs on the
// UI thread while a plugin worker is synchronously waiting for that thread.
class DownloadOperationContext
{
public:
  class OperationLease
  {
  public:
    OperationLease() = default;
    OperationLease(const OperationLease&) = delete;
    OperationLease& operator=(const OperationLease&) = delete;

    OperationLease(OperationLease&& other) noexcept
        : m_Context(std::exchange(other.m_Context, nullptr))
    {}

    OperationLease& operator=(OperationLease&& other) noexcept
    {
      if (this != &other) {
        release();
        m_Context = std::exchange(other.m_Context, nullptr);
      }
      return *this;
    }

    ~OperationLease() { release(); }

    explicit operator bool() const noexcept { return m_Context != nullptr; }

  private:
    explicit OperationLease(const DownloadOperationContext* context) noexcept
        : m_Context(context)
    {}

    void release() noexcept
    {
      if (m_Context != nullptr) {
        m_Context->m_OperationState.fetch_sub(1, std::memory_order_acq_rel);
        m_Context = nullptr;
      }
    }

    const DownloadOperationContext* m_Context{nullptr};
    friend class DownloadOperationContext;
  };

  using MutationLease = SettingsWriteBarrier::MutationLease;

  MutationLease enterMutationIfAllowed() const
  {
    return m_MutationBarrier.enterIfAllowed();
  }

  OperationLease beginOperationIfAllowed() const noexcept
  {
    auto state = m_OperationState.load(std::memory_order_acquire);
    for (;;) {
      if ((state & kAdmissionSuppressed) != 0 ||
          (state & kOperationCountMask) == kOperationCountMask) {
        return {};
      }

      if (m_OperationState.compare_exchange_weak(
              state, state + 1, std::memory_order_acq_rel,
              std::memory_order_acquire)) {
        return OperationLease(this);
      }
    }
  }

  void suppressAdmissionForFailedRollback() noexcept
  {
    // Close operation admission first. A mutation that entered immediately
    // before the mutation barrier closes is still counted by that barrier and
    // can only either finish registering its operation or reject it.
    m_OperationState.fetch_or(kAdmissionSuppressed, std::memory_order_acq_rel);
    m_MutationBarrier.suppress();
  }

  bool mutationsDrainedForFailedRollback() const noexcept
  {
    return m_MutationBarrier.suppressionDrained();
  }

  bool operationsDrainedForFailedRollback() const noexcept
  {
    return mutationsDrainedForFailedRollback() && activeOperationCount() == 0;
  }

  bool admissionSuppressed() const noexcept
  {
    return (m_OperationState.load(std::memory_order_acquire) &
            kAdmissionSuppressed) != 0;
  }

  std::size_t activeOperationCount() const noexcept
  {
    return static_cast<std::size_t>(
        m_OperationState.load(std::memory_order_acquire) & kOperationCountMask);
  }

private:
  static constexpr std::uint64_t kAdmissionSuppressed =
      std::uint64_t{1} << (std::numeric_limits<std::uint64_t>::digits - 1);
  static constexpr std::uint64_t kOperationCountMask =
      kAdmissionSuppressed - 1;

  SettingsWriteBarrier m_MutationBarrier;
  mutable std::atomic_uint64_t m_OperationState{0};
};
