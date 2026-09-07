// SPDX-License-Identifier: MIT
#include "Interface/Core/LookupCache.h"
#include "Interface/Core/SharedCodeBufferManager.h"

#include <FEXCore/fextl/memory.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Utils/MathUtils.h>

#ifndef _WIN32
#include <FEXCore/Utils/PrctlUtils.h>
#endif

namespace FEXCore::CPU {
static constexpr size_t INITIAL_CODE_SIZE = 1024 * 1024 * 16;
// We don't want to move above 128MB atm because that means we will have to encode longer jumps
static constexpr size_t MAX_CODE_SIZE = 1024 * 1024 * 128;

CodeBuffer::CodeBuffer(size_t Size)
  : AllocatedSize(Size)
  , GuardSize(FEXCore::Utils::HostPageSize()) {
  // The guard must be a host page: mprotect rounds to host page granularity, so reserving only
  // FEX_PAGE_SIZE (4096) on a 16k host would either fail outright on the unaligned address or
  // protect 16k while UsableSize() still handed out the first 12k of it as code space.
  LOGMAN_THROW_A_FMT(Size > GuardSize, "Code buffer of {} bytes cannot hold a {} byte guard", Size, GuardSize);

  Ptr = static_cast<uint8_t*>(FEXCore::Allocator::VirtualAlloc(Size, true));
  LOGMAN_THROW_A_FMT(!!Ptr, "Couldn't allocate code buffer");

  // Protect the last host page of the allocated buffer to trigger SIGSEGV on write access.
  // mmap returns host-page-aligned memory, so aligning the guard start down by the host page size
  // keeps it inside this allocation and lands on a boundary mprotect accepts.
  uintptr_t LastPageAddr = AlignDown(reinterpret_cast<uintptr_t>(Ptr) + Size - 1, GuardSize);
  if (!FEXCore::Allocator::VirtualProtect(reinterpret_cast<void*>(LastPageAddr), GuardSize, FEXCore::Allocator::ProtectOptions::None)) {
    // Continuing without the guard is worse than failing here: a JIT overrun would silently
    // corrupt whatever follows the buffer instead of raising a catchable SIGSEGV, turning a
    // localized bug into memory corruption diagnosed far from its cause.
    ERROR_AND_DIE_FMT("Failed to mprotect {} byte guard page at 0x{:x} of code buffer", GuardSize, LastPageAddr);
  }

  FEXCore::Allocator::VirtualName("FEXMemJIT", Ptr, Size);

  // Huge-pages reduce the amount of iTLB misses dramatically when it works.
  FEXCore::Allocator::VirtualTHPControl(Ptr, Size, FEXCore::Allocator::THPControl::Enable);

  LookupCache = fextl::make_unique<GuestToHostMap>();

  CodeBufferEnd = Ptr + UsableSize();
  CodeBufferOffset = Ptr;
}

CodeBuffer::~CodeBuffer() {
  FEXCore::Allocator::VirtualFree(Ptr, AllocatedSize);
}

fextl::shared_ptr<CodeBuffer> SharedCodeBufferManager::AllocateNew(size_t Size) {
#ifndef _WIN32
  // MDWE (Memory-Deny-Write-Execute) is a new Linux 6.3 feature.
  // It's equivalent to systemd's `MemoryDenyWriteExecute` but implemented entirely in the kernel.
  //
  // MDWE prevents applications from creating RWX memory mappings.
  // This prevents FEX from doing anything JIT related, as FEX uses RWX for JIT memory mappings.
  //
  // A potential workaround to make FEX work with MDWE is to call mprotect every time we need to write or modify code.
  // Alternatively, FEX could use a memory mirror where one half is mapped as RW and the other is RX.
  //
  // Once MDWE is enabled with the prctl, the feature is sealed and it can /NOT/ be turned off.
  //
  // Status of MDWE is queried through prctl using `PR_GET_MDWE`:
  // -1: The kernel doesn't support MDWE
  // 0: MDWE is supported but disabled
  // >0: MDWE is enabled, hence prohibiting RWX mappings
  int MDWE = ::prctl(PR_GET_MDWE, 0, 0, 0, 0);
  if (MDWE != -1 && MDWE != 0) {
    LogMan::Msg::EFmt("MDWE was set to 0x{:x} which means FEX can't allocate executable memory", MDWE);
  }
#endif

  auto Buffer = fextl::make_shared<CodeBuffer>(Size);

  Latest = Buffer;

  OnCodeBufferAllocated(Buffer);

  return Buffer;
}

fextl::shared_ptr<CodeBuffer> SharedCodeBufferManager::GetLatest() {
  if (!Latest) {
    AllocateNew(INITIAL_CODE_SIZE);
  }
  return Latest;
}

fextl::shared_ptr<CodeBuffer> SharedCodeBufferManager::StartLargerCodeBuffer() {
  if (!Latest) {
    // Allocate initial CodeBuffer and return it
    return GetLatest();
  }

  auto NewCodeBufferSize = GetLatest()->TotalAllocationSize();
  NewCodeBufferSize = std::min<size_t>(NewCodeBufferSize * 2, MAX_CODE_SIZE);
  return AllocateNew(NewCodeBufferSize);
}

fextl::shared_ptr<CodeBuffer> SharedCodeBufferManager::StartMaximalCodeBuffer() {
  return AllocateNew(MAX_CODE_SIZE);
}
} // namespace FEXCore::CPU
