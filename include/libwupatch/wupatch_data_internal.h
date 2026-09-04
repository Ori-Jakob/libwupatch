#pragma once

#include <stdint.h>

#include "libwupatch/wupatch_config.h"
#include "libwupatch/wupatch_data.h"
#include "libwupatch/wupatch_overlap.h"
#include "libwupatch/wupatch_types.h"

namespace WuPatch {
namespace Data {

bool Dirty();

// Every swap that is wanted or applied, as extents tagged `kind`.
int  CollectExtents(Overlap::Extent* out, int cap, uint8_t kind);
void MarkCollided(int index);

// `ready` false marks the wanted ones blocked instead of applying.
void Reconcile(const Config& cfg, bool ready);
int  RemoveAll();
void Forget();   // process gone; drop state and intent, touch no memory

} // namespace Data
} // namespace WuPatch
