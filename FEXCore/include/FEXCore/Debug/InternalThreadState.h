// SPDX-License-Identifier: MIT
#pragma once

#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/Utils/TypeDefines.h>
#include <FEXCore/Utils/LongJump.h>
#include <FEXCore/fextl/memory.h>
#include <FEXCore/fextl/vector.h>

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <type_traits>

namespace FEXCore {
class LookupCache;
struct JITSymbolBuffer;
} // namespace FEXCore

namespace FEXCore::Context {
class Context;
}

namespace FEXCore::CPU {
class CPUBackend;
} // namespace FEXCore::CPU

namespace FEXCore::Frontend {
class Decoder;
}

namespace FEXCore::IR {
class OpDispatchBuilder;
class PassManager;
} // namespace FEXCore::IR

namespace FEXCore::SHMStats {
struct ThreadStats;
};

namespace FEXCore::Core {

// Special-purpose replacement for std::unique_ptr to allow InternalThreadState to be standard layout.
// Since a NonMovableUniquePtr is neither copyable nor movable, its only function is to own and release the contained object.
template<typename T>
struct NonMovableUniquePtr {
  NonMovableUniquePtr() noexcept = default;
  NonMovableUniquePtr(const NonMovableUniquePtr&) = delete;
  NonMovableUniquePtr& operator=(const NonMovableUniquePtr& UPtr) = delete;

  NonMovableUniquePtr& operator=(fextl::unique_ptr<T> UPtr) noexcept {
    Ptr = UPtr.release();
    return *this;
  }

  ~NonMovableUniquePtr() {
    fextl::default_delete<T> {}(Ptr);
  }

  T* operator->() const noexcept {
    return Ptr;
  }

  std::add_lvalue_reference_t<T> operator*() const noexcept {
    return *Ptr;
  }

  T* get() const noexcept {
    return Ptr;
  }

  explicit operator bool() const noexcept {
    return Ptr != nullptr;
  }

private:
  T* Ptr = nullptr;
};
static_assert(!std::is_move_constructible_v<NonMovableUniquePtr<int>>);
static_assert(!std::is_move_assignable_v<NonMovableUniquePtr<int>>);

// Store used for unaligned LDAXR*/STLXR* emulation.
struct UnalignedExclusiveStore {
  uint64_t Addr;
  uint64_t Store;
  uint8_t Size;
};

// Aligned to FEX_MAX_HOST_PAGE_SIZE so that the allocation itself lands on a host page boundary.
// The InterruptFaultPage member below is mprotect'd independently of the rest of the object, which
// requires the object base to be host-page-aligned; 4096 alignment leaves the fault page starting
// mid-page on a 16k host and mprotect then fails with EINVAL.
struct alignas(FEXCore::Utils::FEX_MAX_HOST_PAGE_SIZE) InternalThreadState : public FEXCore::Allocator::FEXAllocOperators {
  FEXCore::Core::CpuStateFrame* const CurrentFrame = &BaseFrameState;

  FEXCore::Context::Context* const CTX;

  NonMovableUniquePtr<FEXCore::IR::OpDispatchBuilder> OpDispatcher;

  NonMovableUniquePtr<FEXCore::CPU::CPUBackend> CPUBackend;
  NonMovableUniquePtr<FEXCore::LookupCache> LookupCache;

  NonMovableUniquePtr<FEXCore::Frontend::Decoder> FrontendDecoder;
  NonMovableUniquePtr<FEXCore::IR::PassManager> PassManager;
  NonMovableUniquePtr<JITSymbolBuffer> SymbolBuffer;

  // This pointer is owned by the frontend.
  FEXCore::SHMStats::ThreadStats* ThreadStats {};

  UnalignedExclusiveStore ExclusiveStore;

  ///< Data pointer for exclusive use by the frontend
  void* FrontendPtr;

  static constexpr size_t CALLRET_STACK_SIZE {0x400000};

  // The low address of the call-ret stack allocation (not including guard pages)
  void* CallRetStackBase {};

  uintptr_t JITGuardPage {};
  uint64_t JITGuardOverflowArgument {};
  FEXCore::UncheckedLongJump::JumpBuf RestartJump;

  // BaseFrameState should always be at the end, directly before the interrupt fault page
  FEXCore::Core::CpuStateFrame BaseFrameState {};

  // Can be reprotected as RO to trigger an interrupt at generated code block entrypoints.
  //
  // Aligned and sized to FEX_MAX_HOST_PAGE_SIZE rather than FEX_PAGE_SIZE: mprotect operates on
  // host page granularity, so on a 16k host a 4096-aligned member starts mid-page and mprotect
  // fails with EINVAL, leaving the interrupt mechanism unable to fault. alignas needs a
  // compile-time constant, so this uses the maximum supported host page instead of the runtime
  // value; on a 4k host it merely over-reserves.
  //
  // This must stay an inline member: the JIT encodes its offset from BaseFrameState as an
  // immediate (see Arm64JITCore::EmitSuspendInterruptCheck), so it cannot become a separate
  // mapping without also changing code generation.
  alignas(FEXCore::Utils::FEX_MAX_HOST_PAGE_SIZE) uint8_t InterruptFaultPage[FEXCore::Utils::FEX_MAX_HOST_PAGE_SIZE];
};
static_assert(std::is_standard_layout_v<FEXCore::Core::InternalThreadState>);
// Maximum unsigned-offset store range for fault page.
static_assert(
  (offsetof(FEXCore::Core::InternalThreadState, InterruptFaultPage) - offsetof(FEXCore::Core::InternalThreadState, BaseFrameState)) <= 65520,
  "Fault page is outside of immediate range from CPU state");
// The fault page must cover at least one whole host page for mprotect to isolate it from
// neighbouring members; protecting less would round up and strip permissions from BaseFrameState.
static_assert(sizeof(FEXCore::Core::InternalThreadState::InterruptFaultPage) >= FEXCore::Utils::FEX_MAX_HOST_PAGE_SIZE,
              "Interrupt fault page must be at least one host page");

} // namespace FEXCore::Core
