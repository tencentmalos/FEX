// SPDX-License-Identifier: MIT
#pragma once
#include <FEXCore/Utils/CompilerDefs.h>

#include <cstddef>

namespace FEXCore::Utils {
// FEX assumes an operating page size of 4096
// To work around build systems that build on a 16k/64k page size, define our page size here
// Don't use the system provided PAGE_SIZE define because of this.
constexpr size_t FEX_PAGE_SIZE = 4096;
constexpr size_t FEX_PAGE_SHIFT = 12;
constexpr size_t FEX_PAGE_MASK = ~(FEX_PAGE_SIZE - 1);

// Largest host page size this build can support, used where a compile-time constant is required
// (alignas, and the size of an in-struct guard region) and a runtime value cannot be used.
//
// Capped at 16384 rather than 65536 because of the JIT's immediate-offset budget: the interrupt
// fault page must stay within 65520 bytes of BaseFrameState so the offset fits the store immediate
// the JIT emits, and aligning to 65536 pushes it to exactly 65536 -- past the limit. A 64k-page
// host therefore needs the JIT store rewritten to a register-offset form, which is a separate
// change; SetHostPageSize rejects such a host at runtime instead of miscompiling silently.
constexpr size_t FEX_MAX_HOST_PAGE_SIZE = 16384;

// The page size of the *host* kernel, which is distinct from FEX_PAGE_SIZE above.
//
// FEX_PAGE_SIZE is the guest ABI granularity and must stay 4096 regardless of host: guest mmap
// alignment, guest page permission granularity and the code-invalidation index all derive from it.
// This value instead describes what the host kernel will accept for mmap/mprotect boundaries, and
// is 16384 on a 16k-page arm64 target.
//
// Only use this for arguments handed to the host kernel. Using it for guest-visible granularity
// would change guest ABI; using FEX_PAGE_SIZE for a host mprotect boundary fails with EINVAL when
// the host page is larger. The two are equal on a 4k host, which is why conflating them goes
// unnoticed there.
//
// Defaults to FEX_PAGE_SIZE so a frontend that never calls SetHostPageSize keeps 4k behaviour.
FEX_DEFAULT_VISIBILITY size_t HostPageSize();

// Records the host kernel page size, which the caller must query (e.g. sysconf(_SC_PAGESIZE)).
// FEXCore does not query it itself so that it stays free of a libc dependency here.
//
// Must be a power of two, at least FEX_PAGE_SIZE and at most FEX_MAX_HOST_PAGE_SIZE; anything else
// cannot satisfy the guest ABI granularity or the JIT offset budget and is rejected rather than
// silently accepted. Call before creating a Context. Returns false if the value was rejected.
FEX_DEFAULT_VISIBILITY bool SetHostPageSize(size_t Size);
} // namespace FEXCore::Utils
