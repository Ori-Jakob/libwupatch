#pragma once

#include <stdint.h>

#include "libwupatch/wupatch_types.h"

namespace WuPatch {
namespace Data {

static const int kMaxSwaps = 8;
static const int kMaxSlots = 32;

struct SwapDesc {
    const char* owner;
    uint32_t    linkAddr;    // link-time address of the first slot
    uint32_t    stride;      // bytes between slots; 4 for a plain pointer array
    uint16_t    count;       // slots, at most kMaxSlots
    uint16_t    wordOffset;  // byte offset of the word inside each slot
    uint32_t    value;       // written into every slot
};

Handle Declare(const SwapDesc& desc);
void   SetEnabled(Handle h, bool enabled);
State  GetState(Handle h);

int         Count();
const char* OwnerAt(int index);
State       StateAt(int index);
uint32_t    LinkAddrAt(int index);
uint32_t    RuntimeAddrAt(int index);   // 0 until applied at least once

} // namespace Data
} // namespace WuPatch
