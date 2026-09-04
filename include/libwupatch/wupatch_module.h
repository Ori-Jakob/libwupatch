#pragma once

#include <stdint.h>

namespace WuPatch {
namespace Module {

// The text base must come from the loader: .text starts 0x20 past the segment.
bool Find(const char* suffix, uint32_t* textAddr, uint32_t* textSize);

// The patcher crashes in strlen on an unnamed module, so the backend asks first.
int CountUnnamed();

} // namespace Module
} // namespace WuPatch
