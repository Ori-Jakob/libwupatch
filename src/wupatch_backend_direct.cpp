#include "libwupatch/wupatch_backend.h"

#include "libwupatch/wupatch_log.h"
#include "libwupatch/wupatch_ppc.h"

#include <coreinit/cache.h>

namespace WuPatch {

static bool directInit(const Config&)
{
    return true;
}

static void writeWord(const Config& cfg, uint32_t addr, uint32_t word)
{
    if (cfg.writeFn)
        cfg.writeFn((void*)(uintptr_t)addr, &word, sizeof(word));
    else
        *(volatile uint32_t*)(uintptr_t)addr = word;
    DCFlushRange((void*)(uintptr_t)addr, sizeof(word));
    ICInvalidateRange((void*)(uintptr_t)addr, sizeof(word));
    OSMemoryBarrier();
}

static bool encodeSite(const SiteRecord& s, uint32_t* out)
{
    switch (s.shape) {
    case SHAPE_JUMP:
        return Ppc::EncodeBranch(s.runtimeAddr, s.entryAddr, false, out) ||
               Ppc::EncodeAbsoluteBranch(s.entryAddr, false, out);
    case SHAPE_CALL:
        return Ppc::EncodeBranch(s.runtimeAddr, s.entryAddr, true, out) ||
               Ppc::EncodeAbsoluteBranch(s.entryAddr, true, out);
    case SHAPE_REWRITE:
        *out = s.replacement;
        return true;
    }
    return false;
}

static bool directApply(SiteRecord& s, const Config& cfg, uint32_t, const char* owner)
{
    uint32_t word = 0;
    // An out-of-range branch is refused, never bridged through the title.
    if (!encodeSite(s, &word)) {
        Log::Error("%s: dispatcher %08X is out of branch range from %08X and not "
                   "absolutely addressable -- refused", owner,
                   (unsigned)s.entryAddr, (unsigned)s.runtimeAddr);
        return false;
    }

    s.savedWord = s.liveWord;
    s.installedWord = word;
    writeWord(cfg, s.runtimeAddr, word);

    const uint32_t after = *(const volatile uint32_t*)(uintptr_t)s.runtimeAddr;
    if (after != word) {
        Log::Error("%s: wrote %08X to %08X but read back %08X -- is this "
                   "memory writable here?", owner, (unsigned)word,
                   (unsigned)s.runtimeAddr, (unsigned)after);
        return false;
    }
    return true;
}

static bool directRemove(SiteRecord& s, const Config& cfg, const char* owner)
{
    const uint32_t live = *(const volatile uint32_t*)(uintptr_t)s.runtimeAddr;
    if (live != s.installedWord) {
        // Someone patched over us; restoring would stamp on theirs.
        Log::Warn("%s: %08X holds %08X, not our %08X -- leaving it alone",
                  owner, (unsigned)s.runtimeAddr, (unsigned)live,
                  (unsigned)s.installedWord);
        return false;
    }
    writeWord(cfg, s.runtimeAddr, s.savedWord);
    return *(const volatile uint32_t*)(uintptr_t)s.runtimeAddr == s.savedWord;
}

static void directForget(SiteRecord& s)
{
    s.installedWord = 0;
    s.savedWord = 0;
}

static const BackendOps s_ops = { "direct", directInit, directApply, directRemove, directForget };

const BackendOps* DirectBackend() { return &s_ops; }

} // namespace WuPatch
