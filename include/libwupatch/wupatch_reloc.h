#pragma once

#include <stdint.h>

namespace WuPatch {
namespace Reloc {

// Folds (dataDelta & 0xFFFF) into D-form loads and stores (32..55), addi, ori.
uint32_t RelocateLowHalf(uint32_t word, uint32_t dataDelta);

// Accepts hi and hi+1: the carry out of the low half is in another instruction.
bool MatchesHighHalf(uint32_t found, uint32_t word, uint32_t dataDelta);

// True when `found` is the native word in any form: image, low-half, high-half.
bool MatchesNative(uint32_t found, uint32_t expected, uint32_t dataDelta);

} // namespace Reloc
} // namespace WuPatch
