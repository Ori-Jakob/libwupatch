#pragma once

#include <stdint.h>

#include "libwupatch/wupatch_chain.h"
#include "libwupatch/wupatch_config.h"
#include "libwupatch/wupatch_types.h"

namespace WuPatch {

// One instruction, patched once; each caller's hook attaches as a chain link.
struct SiteRecord {
    bool      used;
    int       index;           // row; also the site shim slot
    uint32_t  linkAddr;
    uint32_t  expected;        // the native word, from the link that installed the site
    Shape     shape;           // fixed once the slot is committed
    uint32_t  replacement;     // REWRITE only; fixed once the slot is committed
    State     state;
    uint32_t  runtimeAddr;     // linkAddr + textDelta; valid once guarded
    uint32_t  liveWord;        // what the guard last read at the site
    bool      external;        // linkAddr is already the runtime address
    char      functionName[kFunctionNameChars];   // copied; the declarer may unload
    uint32_t  library;
    uint32_t  installedWord;   // direct backend: the word we wrote
    uint32_t  savedWord;       // direct backend: the word we overwrote
    uint32_t  fpHandle;
    uint32_t  fpThunk;         // FunctionPatcher's own original stub; logged, never used
    bool      committed;       // the slot holds words for this runtimeAddr/liveWord/shape
    uint32_t  entryAddr;       // what the backend branches to: dispatcher, or REWRITE shim
    uint32_t  originalThunk;   // displaced word then site+4; 0 when the word is not PIC
    volatile uint32_t head;    // the dispatcher jumps through this
    Chain::Site chain;
    uint32_t  refusedOn;       // last live word a refusal was logged for
    bool      everRefused;
};

struct PatchRecord {
    Desc      desc;            // desc.owner points at ownerBuf
    char      ownerBuf[kOwnerChars];
    int       index;           // row in the registry; also the link shim slot
    bool      used;
    bool      retired;         // undeclared, but its slot was committed this process
    bool      wanted;
    State     state;
    int       site;            // SiteRecord row, -1 until bound
    volatile uint32_t nextTarget;   // this link's forwarding word
    uint32_t  nextShim;        // jumps through nextTarget; what GetOriginalThunk returns
    uint32_t  callShim;        // CALL only: LR = site+4 then the hook
    bool      shimCommitted;
    uint32_t  nextTryTick;     // do not re-read a refused site before this
};

struct BackendOps {
    const char* name;
    bool (*init)(const Config& cfg);   // false: not available in this process
    // Called with the site guarded and prepared, and the head naming the first link.
    bool (*apply)(SiteRecord& s, const Config& cfg, uint32_t moduleTextAddr, const char* owner);
    bool (*remove)(SiteRecord& s, const Config& cfg, const char* owner);
    void (*forget)(SiteRecord& s);     // process gone; drop handles, touch no memory
};

// NULL where the build has no FunctionPatcher.
const BackendOps* FunctionPatcherBackend();
const BackendOps* DirectBackend();

} // namespace WuPatch
