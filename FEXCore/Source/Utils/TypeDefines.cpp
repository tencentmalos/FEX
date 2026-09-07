// SPDX-License-Identifier: MIT
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Utils/TypeDefines.h>

#include <atomic>
#include <cstddef>

namespace FEXCore::Utils {
namespace {
  // Defaults to the guest ABI page size so that a frontend which never calls SetHostPageSize keeps
  // the historical 4k behaviour instead of reading zero.
  //
  // Relaxed atomic rather than a plain size_t: this is written once during startup but read from
  // JIT-owned threads, so a plain object would be a data race even though every read observes the
  // same value in practice.
  std::atomic<size_t> HostPageSizeValue {FEX_PAGE_SIZE};
} // namespace

size_t HostPageSize() {
  return HostPageSizeValue.load(std::memory_order_relaxed);
}

bool SetHostPageSize(size_t Size) {
  // A host page smaller than the guest ABI granularity cannot back guest page permissions, and a
  // non-power-of-two size breaks every alignment computation downstream. Reject rather than accept
  // a value that would produce a subtly wrong mprotect boundary later.
  if (Size < FEX_PAGE_SIZE || (Size & (Size - 1)) != 0) {
    LogMan::Msg::EFmt("Rejecting host page size {}: must be a power of two and at least {}", Size, FEX_PAGE_SIZE);
    return false;
  }

  // Above FEX_MAX_HOST_PAGE_SIZE the interrupt fault page no longer fits the JIT's immediate offset
  // budget (see FEX_MAX_HOST_PAGE_SIZE). Fail loudly here: accepting the value would leave the
  // interrupt mechanism silently unable to fault, disabling pause and deferred signal handling.
  if (Size > FEX_MAX_HOST_PAGE_SIZE) {
    LogMan::Msg::EFmt("Rejecting host page size {}: this build supports at most {}. A larger host page requires the JIT "
                      "interrupt-fault store to use a register offset.",
                      Size, FEX_MAX_HOST_PAGE_SIZE);
    return false;
  }

  HostPageSizeValue.store(Size, std::memory_order_relaxed);
  return true;
}
} // namespace FEXCore::Utils
