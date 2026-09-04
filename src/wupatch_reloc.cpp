#include "libwupatch/wupatch_reloc.h"

#include "libwupatch/wupatch_ppc.h"

namespace WuPatch {
namespace Reloc {

// addi, ori, and the D-form loads and stores.
static bool carriesLowHalf(unsigned opcode)
{
    return opcode == 14 || opcode == 24 || (opcode >= 32 && opcode <= 55);
}

uint32_t RelocateLowHalf(uint32_t word, uint32_t dataDelta)
{
    if (!carriesLowHalf(Ppc::PrimaryOpcode(word)))
        return word;
    return (word & 0xFFFF0000u) | ((word + (dataDelta & 0xFFFFu)) & 0xFFFFu);
}

bool MatchesHighHalf(uint32_t found, uint32_t word, uint32_t dataDelta)
{
    if (Ppc::PrimaryOpcode(word) != 15)
        return false;
    const uint32_t head = word & 0xFFFF0000u;
    const uint32_t imm  = word & 0x0000FFFFu;
    const uint32_t hi   = dataDelta >> 16;
    return found == (head | ((imm + hi) & 0xFFFFu)) ||
           found == (head | ((imm + hi + 1u) & 0xFFFFu));
}

bool MatchesNative(uint32_t found, uint32_t expected, uint32_t dataDelta)
{
    if (found == expected)
        return true;
    if (dataDelta == 0)
        return false;
    return found == RelocateLowHalf(expected, dataDelta) ||
           MatchesHighHalf(found, expected, dataDelta);
}

} // namespace Reloc
} // namespace WuPatch
