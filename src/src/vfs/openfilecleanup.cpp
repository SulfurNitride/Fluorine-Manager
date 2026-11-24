#include "mo2filesystem.h"

#include <unistd.h>

Mo2FsContext::~Mo2FsContext()
{
  (void)mo2CloseOpenFiles(this);
}

Mo2OpenFileCleanupResult mo2CloseOpenFiles(Mo2FsContext* ctx) noexcept
{
  Mo2OpenFileCleanupResult result;
  if (ctx == nullptr) {
    return result;
  }

  std::scoped_lock const lock(ctx->open_files_mutex);
  result.logical_handles = ctx->open_files.size();
  for (auto& [handle, openFile] : ctx->open_files) {
    (void)handle;
    if (openFile.fd < 0) {
      continue;
    }

    ++result.descriptors_closed;
    if (openFile.writable) {
      ++result.writable_descriptors_closed;
    }

    // On Linux the descriptor must not be retried after close(), including
    // EINTR: it may already have been released and reused by another thread.
    if (::close(openFile.fd) != 0) {
      ++result.close_errors;
    }
    openFile.fd = -1;
  }
  ctx->open_files.clear();
  ctx->dirty_metadata_handles.store(0, std::memory_order_release);
  return result;
}
