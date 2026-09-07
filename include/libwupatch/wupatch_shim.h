#pragma once

#include <stdint.h>

namespace WuPatch {
namespace Shim {

// A SHIM MUST NOT CLOBBER A REGISTER THE PATCHED FUNCTION WOULD HAVE LEFT ALONE.
// r11 is volatile, so an ABI-conformant caller cannot object to a linkage stub
// using it -- but a caller compiled with interprocedural register allocation
// can, and does. The Wind Waker HD message states hold `this` in r11 and in r8
// across a call to a six-instruction leaf that touches only r0 and r3, then read
// it back afterwards. A dispatcher that ate r11 turned those into crashes in
// game code several frames later. So the stubs below borrow r11 through a frame
// of their own and hand it back before branching; only CTR is spent, which an
// indirect branch cannot avoid.
static const int kSlotWords     = 32;   // 128 bytes: four cache lines, never shared
static const int kCallWords     = 7;
static const int kRewriteWords  = 9;
static const int kIndirectWords = 9;

// The second shim starts on the third cache line, clear of a nine-word first
// shim, so a slot's two shims still commit independently.
static const int kSiteEntryOffset = 0;   // site slot: dispatcher, or the REWRITE shim
static const int kSiteThunkOffset = 16;  // site slot: the original-instruction thunk
static const int kLinkNextOffset  = 0;   // link slot: jumps through the link's nextTarget
static const int kLinkCallOffset  = 16;  // link slot: CALL only, LR = site+4 then the hook

// Set LR = ret, then jump to hook; both RUNTIME addresses. Returns words written.
int BuildCall(uint32_t* out, uint32_t ret, uint32_t hook);

// Set CTR = ret, run `replacement`, bctr. With the displaced word, this is the
// thunk. r11 is saved and restored; `replacement` runs with the caller's r11 and
// r1, so a displaced instruction that reads either still sees what it expects.
int BuildRewrite(uint32_t* out, uint32_t ret, uint32_t replacement);

// Load the word at `ptrAddr` and jump to it, so retargeting a chain is one store.
// Preserves r11; spends only CTR.
int BuildIndirect(uint32_t* out, uint32_t ptrAddr);

} // namespace Shim
} // namespace WuPatch
