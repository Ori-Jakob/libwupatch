#pragma once

#include <stdint.h>

namespace WuPatch {

enum Backend {
    BACKEND_AUTO = 0,   // FunctionPatcher, else direct only if allowDirectWrite
    BACKEND_FUNCTION_PATCHER,
    BACKEND_DIRECT,
};

// A privileged copy where the host has one. NULL means a plain store.
typedef void (*WriteFn)(void* dst, const void* src, uint32_t len);

struct Config {
    const uint64_t* titleIds;        // the patcher's own gate
    uint32_t        titleIdCount;
    const char*     executableName;  // suffix-matched against loaded modules, e.g. ".rpx"
    uint16_t        versionMin;      // title version range, inclusive
    uint16_t        versionMax;
    const char*     tag;             // log prefix, e.g. "[wwhd_tools][patch]"

    // Both segments relocate by different amounts: textDelta finds a site, dataDelta reads it.
    uint32_t textDelta;
    bool     textResolved;
    uint32_t dataDelta;
    bool     dataResolved;

    Backend  backend;
    bool     allowDirectWrite;       // defaults false

    // Skip the OSIsAddressValid guard. For a text site that guard is already
    // redundant - the site was proved to sit inside the module's own text - and
    // an emulator that does not implement the call refuses every patch on it.
    // Data swaps have no such proof, so only set this where the host knows the
    // addresses are sound.
    bool     assumeMapped;
    WriteFn  writeFn;                // optional; direct backend only

    // Works around the patcher crashing on unnamed modules, but drops its title gate.
    bool     fpByPhysicalAddress;
};

} // namespace WuPatch
