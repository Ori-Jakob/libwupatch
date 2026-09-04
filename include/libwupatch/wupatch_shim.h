#pragma once

#include <stdint.h>

namespace WuPatch {
namespace Shim {

static const int kSlotWords     = 16;   // 64 bytes: two cache lines, never shared
static const int kCallWords     = 7;
static const int kRewriteWords  = 5;
static const int kIndirectWords = 5;

// Word 8 starts the second cache line, so a slot's two shims commit independently.
static const int kSiteEntryOffset = 0;   // site slot: dispatcher, or the REWRITE shim
static const int kSiteThunkOffset = 8;   // site slot: the original-instruction thunk
static const int kLinkNextOffset  = 0;   // link slot: jumps through the link's nextTarget
static const int kLinkCallOffset  = 8;   // link slot: CALL only, LR = site+4 then the hook

// Set LR = ret, then jump to hook; both RUNTIME addresses. Returns words written.
int BuildCall(uint32_t* out, uint32_t ret, uint32_t hook);

// Set CTR = ret, run `replacement`, bctr. With the displaced word, this is the thunk.
int BuildRewrite(uint32_t* out, uint32_t ret, uint32_t replacement);

// Load the word at `ptrAddr` and jump to it, so retargeting a chain is one store.
int BuildIndirect(uint32_t* out, uint32_t ptrAddr);

} // namespace Shim
} // namespace WuPatch
