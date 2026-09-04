#include "libwupatch/wupatch_apply.h"

#include "libwupatch/wupatch_log.h"
#include "libwupatch/wupatch_module.h"
#include "libwupatch/wupatch_ppc.h"
#include "libwupatch/wupatch_reloc.h"
#include "libwupatch/wupatch_shim.h"

#include <coreinit/cache.h>
#include <coreinit/memorymap.h>

namespace WuPatch {
namespace Apply {

static const BackendOps* s_backend = 0;
static bool     s_backendChecked = false;
static uint32_t s_textAddr = 0;
static uint32_t s_textSize = 0;
static bool     s_reportedNoModule = false;

// 32-byte aligned, 64 bytes per slot: no shim shares a cache line with another.
static uint32_t s_sitePool[kMaxSites][Shim::kSlotWords] __attribute__((aligned(32)));
static uint32_t s_linkPool[kMaxPatches][Shim::kSlotWords] __attribute__((aligned(32)));

static const BackendOps* selectBackend(const Config& cfg)
{
    const BackendOps* fp = FunctionPatcherBackend();
    const BackendOps* direct = DirectBackend();

    switch (cfg.backend) {
    case BACKEND_FUNCTION_PATCHER:
        return (fp && fp->init(cfg)) ? fp : 0;
    case BACKEND_DIRECT:
        return (direct && direct->init(cfg)) ? direct : 0;
    case BACKEND_AUTO:
    default:
        if (fp && fp->init(cfg))
            return fp;
        // Never fall through to a raw write unless the host opted in.
        if (cfg.allowDirectWrite && direct && direct->init(cfg))
            return direct;
        return 0;
    }
}

bool EnsureReady(const Config& cfg)
{
    if (!s_backendChecked) {
        s_backendChecked = true;
        s_backend = selectBackend(cfg);
        if (s_backend)
            Log::Info("backend: %s", s_backend->name);
        else
            Log::Error("no backend available -- nothing will be patched");
    }
    if (!s_backend)
        return false;

    if (!s_textAddr) {
        if (!Module::Find(cfg.executableName, &s_textAddr, &s_textSize)) {
            if (!s_reportedNoModule) {
                s_reportedNoModule = true;
                Log::Warn("no module ending in '%s' loaded yet", cfg.executableName);
            }
            return false;
        }
        Log::Info("module '%s' text=%08X size=%08X", cfg.executableName,
                  (unsigned)s_textAddr, (unsigned)s_textSize);
    }
    return true;
}

void Reset()
{
    s_backend = 0;
    s_backendChecked = false;
    s_textAddr = 0;
    s_textSize = 0;
    s_reportedNoModule = false;
}

const char* BackendName() { return s_backend ? s_backend->name : ""; }
uint32_t    ModuleTextAddr() { return s_textAddr; }
uint32_t    ModuleTextSize() { return s_textSize; }

static void refuse(SiteRecord& s, uint32_t live, uint32_t dataDelta, const char* owner,
                   const char* why)
{
    s.state = STATE_REFUSED;
    if (s.everRefused && s.refusedOn == live)
        return;
    s.everRefused = true;
    s.refusedOn = live;
    Log::Warn("%s: %08X %s (live %08X, native %08X, data delta %06X)",
              owner, (unsigned)s.runtimeAddr, why, (unsigned)live,
              (unsigned)s.expected, (unsigned)dataDelta);
}

bool GuardSite(SiteRecord& s, const Config& cfg, const char* owner)
{
    // An export is already a runtime address, and is not in the title's text.
    s.runtimeAddr = s.external ? s.linkAddr : s.linkAddr + cfg.textDelta;

    if (!s.external &&
        (s.runtimeAddr < s_textAddr ||
         (s.runtimeAddr - s_textAddr) + 4u > s_textSize)) {
        refuse(s, 0, cfg.dataDelta, owner, "is outside the module's text");
        return false;
    }
    if (s.runtimeAddr & 3u) {
        refuse(s, 0, cfg.dataDelta, owner, "is not word-aligned");
        return false;
    }
    if (!OSIsAddressValid(s.runtimeAddr)) {
        refuse(s, 0, cfg.dataDelta, owner, "is not mapped");
        return false;
    }

    const uint32_t live = *(const volatile uint32_t*)(uintptr_t)s.runtimeAddr;
    // No image to have read a library's word from: the first look defines it.
    if (s.external && !s.expected)
        s.expected = live;
    if (!Reloc::MatchesNative(live, s.expected, cfg.dataDelta)) {
        refuse(s, live, cfg.dataDelta, owner, "does not hold the expected instruction");
        return false;
    }
    if (s.committed && live != s.liveWord) {
        // The slot was committed against another displaced word and cannot be rewritten.
        refuse(s, live, cfg.dataDelta, owner, "changed since its slot was committed");
        return false;
    }

    s.liveWord = live;
    s.everRefused = false;
    return true;
}

bool PrepareSite(SiteRecord& s, const char* owner)
{
    uint32_t* slot = SiteSlot(s.index);
    if (!slot)
        return false;
    if (s.committed)
        return true;

    const uint32_t ret = s.runtimeAddr + 4u;
    if (s.shape == SHAPE_REWRITE) {
        uint32_t* entry = slot + Shim::kSiteEntryOffset;
        const int words = Shim::BuildRewrite(entry, ret, s.replacement);
        if (!CommitShim(entry, words)) {
            Log::Error("%s: rewrite shim did not land", owner);
            return false;
        }
        s.entryAddr = (uint32_t)(uintptr_t)entry;
        s.originalThunk = 0;
    } else {
        // The thunk first, so a chain's tail has somewhere valid to go before going live.
        s.originalThunk = 0;
        if (Ppc::Classify(s.liveWord) == Ppc::INSTR_PIC) {
            uint32_t* thunk = slot + Shim::kSiteThunkOffset;
            const int words = Shim::BuildRewrite(thunk, ret, s.liveWord);
            if (!CommitShim(thunk, words)) {
                Log::Error("%s: original thunk did not land", owner);
                return false;
            }
            s.originalThunk = (uint32_t)(uintptr_t)thunk;
        }
        uint32_t* entry = slot + Shim::kSiteEntryOffset;
        const int words = Shim::BuildIndirect(entry, (uint32_t)(uintptr_t)&s.head);
        if (!CommitShim(entry, words)) {
            Log::Error("%s: dispatcher did not land", owner);
            return false;
        }
        s.entryAddr = (uint32_t)(uintptr_t)entry;
    }
    s.chain.originalThunk = s.originalThunk;
    s.committed = true;
    return true;
}

bool InstallSite(SiteRecord& s, const Config& cfg, const char* owner)
{
    if (!s_backend || !s_textAddr) {
        s.state = STATE_BLOCKED;
        return false;
    }
    if (!s.committed || !s.entryAddr) {
        s.state = STATE_FAILED;
        return false;
    }
    if (s.shape != SHAPE_REWRITE && s.head == 0) {
        Log::Error("%s: site %08X has no chain head", owner, (unsigned)s.runtimeAddr);
        s.state = STATE_FAILED;
        return false;
    }

    if (!s_backend->apply(s, cfg, s_textAddr, owner)) {
        s.state = STATE_FAILED;
        return false;
    }

    s.state = STATE_APPLIED;
    const uint32_t after = *(const volatile uint32_t*)(uintptr_t)s.runtimeAddr;
    Log::Info("%s: %08X (+%06X) %08X -> %08X via %s%s",
              owner, (unsigned)s.runtimeAddr,
              (unsigned)(s.runtimeAddr - s_textAddr), (unsigned)s.liveWord,
              (unsigned)after, s_backend->name,
              (s.shape == SHAPE_REWRITE || s.originalThunk) ? "" : ", no original thunk");
    return true;
}

bool UninstallSite(SiteRecord& s, const Config& cfg, const char* owner)
{
    if (!s_backend)
        return false;
    if (!s_backend->remove(s, cfg, owner)) {
        s.state = STATE_FAILED;
        return false;
    }
    s.state = STATE_DECLARED;
    Log::Info("%s: %08X restored", owner, (unsigned)s.runtimeAddr);
    return true;
}

void ForgetSite(SiteRecord& s)
{
    if (s_backend)
        s_backend->forget(s);
    s.state = STATE_DECLARED;
    s.fpHandle = 0;
    s.fpThunk = 0;
    s.committed = false;
    s.entryAddr = 0;
    s.originalThunk = 0;
    s.head = 0;
    s.chain.originalThunk = 0;
    s.everRefused = false;
}

bool PrepareLink(PatchRecord& r, const SiteRecord& s)
{
    if (s.shape == SHAPE_REWRITE)
        return true;
    if (r.shimCommitted)
        return true;

    uint32_t* slot = LinkSlot(r.index);
    if (!slot)
        return false;

    uint32_t* next = slot + Shim::kLinkNextOffset;
    int words = Shim::BuildIndirect(next, (uint32_t)(uintptr_t)&r.nextTarget);
    if (!CommitShim(next, words)) {
        Log::Error("%s: next stub did not land", r.desc.owner);
        return false;
    }
    r.nextShim = (uint32_t)(uintptr_t)next;

    r.callShim = 0;
    if (s.shape == SHAPE_CALL) {
        uint32_t* call = slot + Shim::kLinkCallOffset;
        words = Shim::BuildCall(call, s.runtimeAddr + 4u, (uint32_t)(uintptr_t)r.desc.hook);
        if (!CommitShim(call, words)) {
            Log::Error("%s: call shim did not land", r.desc.owner);
            return false;
        }
        r.callShim = (uint32_t)(uintptr_t)call;
    }
    r.shimCommitted = true;
    return true;
}

void ForgetLink(PatchRecord& r)
{
    r.shimCommitted = false;
    r.nextShim = 0;
    r.callShim = 0;
    r.nextTryTick = 0;
}

uint32_t* SiteSlot(int index)
{
    if (index < 0 || index >= kMaxSites)
        return 0;
    return s_sitePool[index];
}

uint32_t* LinkSlot(int index)
{
    if (index < 0 || index >= kMaxPatches)
        return 0;
    return s_linkPool[index];
}

bool CommitShim(uint32_t* at, int words)
{
    if (!at || words <= 0 || words > Shim::kSlotWords)
        return false;
    const uint32_t bytes = (uint32_t)words * 4u;
    DCFlushRange(at, bytes);
    ICInvalidateRange(at, bytes);
    OSMemoryBarrier();
    // Volatile read-back: a core branching in early would run whatever was there.
    for (int i = 0; i < words; ++i) {
        if (((const volatile uint32_t*)at)[i] != at[i])
            return false;
    }
    return true;
}

void Publish(volatile uint32_t* word, uint32_t value)
{
    *word = value;
    DCFlushRange((void*)word, sizeof(*word));
    OSMemoryBarrier();
}

} // namespace Apply
} // namespace WuPatch
