#pragma once

#include <stdint.h>

#include "libwupatch/wupatch_backend.h"

namespace WuPatch {
namespace Apply {

// Frames between re-reads of a refused site.
static const uint32_t kRetryTicks = 30;

// Selects the backend and finds the module. Idempotent; false until both are known.
bool EnsureReady(const Config& cfg);
void Reset();   // forget readiness; the next EnsureReady starts over

const char* BackendName();   // "" until ready
uint32_t    ModuleTextAddr();
uint32_t    ModuleTextSize();

// Validates the site against its native word in every relocated form.
bool GuardSite(SiteRecord& s, const Config& cfg, const char* owner);

// Builds the site's shims before anything is written to the title. Idempotent.
bool PrepareSite(SiteRecord& s, const char* owner);

bool InstallSite(SiteRecord& s, const Config& cfg, const char* owner);
bool UninstallSite(SiteRecord& s, const Config& cfg, const char* owner);
void ForgetSite(SiteRecord& s);

// Builds the link's stubs. Idempotent; nothing for a REWRITE site.
bool PrepareLink(PatchRecord& r, const SiteRecord& s);
void ForgetLink(PatchRecord& r);

// One 64-byte slot per site and per row, bound for the life of the process.
uint32_t* SiteSlot(int index);
uint32_t* LinkSlot(int index);
// Flush, invalidate, then read back. False if the words did not land.
bool CommitShim(uint32_t* at, int words);
// Store one word another core will read, and make it visible.
void Publish(volatile uint32_t* word, uint32_t value);

} // namespace Apply
} // namespace WuPatch
