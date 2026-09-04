#pragma once

#include <stdint.h>

// Host-overridable. Neither may exceed 255: overlap rows are indexed by a byte.
#ifndef WUPATCH_MAX_PATCHES
#define WUPATCH_MAX_PATCHES 128
#endif
#ifndef WUPATCH_MAX_SITES
#define WUPATCH_MAX_SITES 96
#endif

namespace WuPatch {

typedef uint32_t Handle;
static const Handle kInvalidHandle = 0;

enum Shape {
    SHAPE_JUMP = 0,   // branch to hook, LR untouched; at a function's first word, a replacement
    SHAPE_CALL,       // hook entered as if by bl: LR = site+4, hook ends in blr
    SHAPE_REWRITE,    // run replacement instead of the site word, continue at site+4
};

enum State {
    STATE_DECLARED = 0,
    STATE_BLOCKED,       // wanted, but the title, deltas or patcher are not ready
    STATE_REFUSED,       // the live word is not this site's native instruction
    STATE_COLLIDED,      // another declared patch claims an overlapping word
    STATE_APPLIED,
    STATE_FAILED,        // the backend refused; see the log
};

enum Segment { SEG_TEXT = 0, SEG_DATA = 1 };

enum Flags {
    FLAG_EXCLUSIVE = 1u << 0,   // refuse to share the site; JUMP and CALL chain otherwise
};

struct Site {
    uint32_t linkAddr;   // LINK-TIME address, as a disassembler records it
    uint32_t expected;   // the word the RPX image holds there
};

static const uint32_t kLibraryByAddress = 0xFFFFFFFFu;

struct Desc {
    const char* owner;        // log prefix and collision attribution; copied, 31 chars kept
    Site        site;
    Shape       shape;
    const void* hook;         // SHAPE_JUMP / SHAPE_CALL
    uint32_t    replacement;  // SHAPE_REWRITE
    uint32_t    flags;        // Flags
    int32_t     priority;     // chain order: higher runs earlier; ties in attach order

    // An export of another module: linkAddr is host-resolved, expected must be 0, JUMP only.
    bool        external;
    const char* functionName;  // the export, for the patcher to resolve again
    uint32_t    library;       // function_replacement_library_type_t, or kLibraryByAddress
};

static const uint32_t kNop = 0x60000000u;   // ori r0, r0, 0

static const int kMaxPatches = WUPATCH_MAX_PATCHES;
static const int kMaxSites   = WUPATCH_MAX_SITES;
static const int kOwnerChars = 32;
static const int kFunctionNameChars = 64;

} // namespace WuPatch
