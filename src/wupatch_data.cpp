#include "libwupatch/wupatch_data.h"
#include "libwupatch/wupatch_data_internal.h"

#include "libwupatch/wupatch_log.h"

#include <coreinit/cache.h>
#include <coreinit/memorymap.h>

namespace WuPatch {
namespace Data {

struct SwapRecord {
    SwapDesc desc;
    bool     used;
    bool     wanted;
    State    state;
    uint32_t runtimeAddr;
    uint32_t saved[kMaxSlots];
};

static SwapRecord s_swaps[kMaxSwaps];
static int        s_count = 0;
static bool       s_dirty = false;

static SwapRecord* at(Handle h)
{
    const int i = (int)h - 1;
    return (i >= 0 && i < s_count && s_swaps[i].used) ? &s_swaps[i] : 0;
}

Handle Declare(const SwapDesc& desc)
{
    if (!desc.owner || !desc.linkAddr || (desc.linkAddr & 3u) ||
        !desc.count || desc.count > kMaxSlots ||
        !desc.stride || (desc.stride & 3u) ||
        (desc.wordOffset & 3u) || desc.wordOffset + 4u > desc.stride) {
        Log::Error("data swap '%s' rejected: malformed descriptor",
                   desc.owner ? desc.owner : "?");
        return kInvalidHandle;
    }
    for (int i = 0; i < s_count; ++i) {
        const SwapDesc& d = s_swaps[i].desc;
        if (d.linkAddr == desc.linkAddr && d.stride == desc.stride &&
            d.count == desc.count && d.wordOffset == desc.wordOffset &&
            d.value == desc.value)
            return (Handle)(i + 1);
    }
    if (s_count >= kMaxSwaps) {
        Log::Error("data swap '%s' rejected: table full (%d)", desc.owner, kMaxSwaps);
        return kInvalidHandle;
    }
    SwapRecord& r = s_swaps[s_count];
    r.desc = desc;
    r.used = true;
    r.wanted = false;
    r.state = STATE_DECLARED;
    r.runtimeAddr = 0;
    return (Handle)(++s_count);
}

void SetEnabled(Handle h, bool enabled)
{
    SwapRecord* r = at(h);
    if (!r || r->wanted == enabled)
        return;
    r->wanted = enabled;
    s_dirty = true;
}

State GetState(Handle h)
{
    const SwapRecord* r = at(h);
    return r ? r->state : STATE_DECLARED;
}

static uint32_t slotAddr(const SwapRecord& r, int slot)
{
    return r.runtimeAddr + (uint32_t)slot * r.desc.stride + r.desc.wordOffset;
}

static bool apply(SwapRecord& r, const Config& cfg)
{
    r.runtimeAddr = r.desc.linkAddr + cfg.dataDelta;
    const uint32_t bytes = (uint32_t)r.desc.count * r.desc.stride;
    if (!cfg.assumeMapped &&
        (!OSIsAddressValid(r.runtimeAddr) ||
         !OSIsAddressValid(r.runtimeAddr + bytes - 1u))) {
        Log::Warn("%s: %08X..%08X is not mapped", r.desc.owner,
                  (unsigned)r.runtimeAddr, (unsigned)(r.runtimeAddr + bytes));
        return false;
    }
    for (int i = 0; i < r.desc.count; ++i) {
        volatile uint32_t* p = (volatile uint32_t*)(uintptr_t)slotAddr(r, i);
        r.saved[i] = *p;
        *p = r.desc.value;
    }
    // Read by game code on another core before the next frame dispatches.
    DCFlushRange((void*)(uintptr_t)r.runtimeAddr, bytes);
    OSMemoryBarrier();
    Log::Info("%s: %d slot(s) at %08X -> %08X", r.desc.owner, (int)r.desc.count,
              (unsigned)r.runtimeAddr, (unsigned)r.desc.value);
    return true;
}

static void remove(SwapRecord& r)
{
    int restored = 0;
    for (int i = 0; i < r.desc.count; ++i) {
        volatile uint32_t* p = (volatile uint32_t*)(uintptr_t)slotAddr(r, i);
        // Anything else was written after us, and is not ours to undo.
        if (*p == r.desc.value) {
            *p = r.saved[i];
            ++restored;
        }
    }
    DCFlushRange((void*)(uintptr_t)r.runtimeAddr, (uint32_t)r.desc.count * r.desc.stride);
    OSMemoryBarrier();
    Log::Info("%s: %d of %d slot(s) restored", r.desc.owner, restored, (int)r.desc.count);
}

bool Dirty() { return s_dirty; }
int  Count() { return s_count; }
const char* OwnerAt(int i)   { return (i >= 0 && i < s_count) ? s_swaps[i].desc.owner : ""; }
State       StateAt(int i)   { return (i >= 0 && i < s_count) ? s_swaps[i].state : STATE_DECLARED; }
uint32_t    LinkAddrAt(int i){ return (i >= 0 && i < s_count) ? s_swaps[i].desc.linkAddr : 0; }
uint32_t    RuntimeAddrAt(int i){ return (i >= 0 && i < s_count) ? s_swaps[i].runtimeAddr : 0; }

int CollectExtents(Overlap::Extent* out, int cap, uint8_t kind)
{
    int n = 0;
    for (int i = 0; i < s_count && n < cap; ++i) {
        SwapRecord& r = s_swaps[i];
        if (!r.used || (!r.wanted && r.state != STATE_APPLIED))
            continue;
        // A resolved collision may proceed; it is re-marked below if not.
        if (r.state == STATE_COLLIDED)
            r.state = STATE_DECLARED;
        Overlap::Extent& e = out[n++];
        e.start   = r.desc.linkAddr;
        e.end     = r.desc.linkAddr + (uint32_t)r.desc.count * r.desc.stride;
        e.segment = SEG_DATA;
        e.kind    = kind;
        e.index   = (uint8_t)i;
        e.applied = r.state == STATE_APPLIED;
    }
    return n;
}

void MarkCollided(int index)
{
    if (index >= 0 && index < s_count && s_swaps[index].state != STATE_APPLIED)
        s_swaps[index].state = STATE_COLLIDED;
}

void Reconcile(const Config& cfg, bool ready)
{
    s_dirty = false;
    for (int i = 0; i < s_count; ++i) {
        SwapRecord& r = s_swaps[i];
        if (!r.used)
            continue;
        if (!r.wanted && r.state == STATE_APPLIED) {
            remove(r);
            r.state = STATE_DECLARED;
        } else if (r.wanted && r.state != STATE_APPLIED && r.state != STATE_COLLIDED) {
            if (!ready) {
                r.state = STATE_BLOCKED;
            } else if (apply(r, cfg)) {
                r.state = STATE_APPLIED;
            } else {
                r.state = STATE_FAILED;
            }
        }
    }
}

int RemoveAll()
{
    int removed = 0;
    for (int i = 0; i < s_count; ++i) {
        if (s_swaps[i].used && s_swaps[i].state == STATE_APPLIED) {
            remove(s_swaps[i]);
            s_swaps[i].state = STATE_DECLARED;
            ++removed;
        }
    }
    return removed;
}

void Forget()
{
    for (int i = 0; i < s_count; ++i) {
        if (s_swaps[i].used) {
            s_swaps[i].state = STATE_DECLARED;
            s_swaps[i].wanted = false;
        }
    }
    s_dirty = true;
}

} // namespace Data
} // namespace WuPatch
