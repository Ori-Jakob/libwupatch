#include "libwupatch/wupatch_ppc.h"

namespace WuPatch {
namespace Ppc {

unsigned PrimaryOpcode(uint32_t word)
{
    return word >> 26;
}

// The SPR field of mtspr/mfspr, whose two five-bit halves are swapped.
static unsigned sprField(uint32_t word)
{
    return ((word >> 16) & 31u) | (((word >> 11) & 31u) << 5);
}

InstrClass Classify(uint32_t word)
{
    switch (PrimaryOpcode(word)) {
    case 16:   // bc
    case 18:   // b
        // AA set means an absolute target, which survives relocation.
        return (word & 2u) ? INSTR_PIC : INSTR_PC_RELATIVE;
    case 19: {
        // bcctr would branch to the CTR the stub loaded, not the game's.
        const unsigned xo = (word >> 1) & 0x3FFu;
        return xo == 528u ? INSTR_UNKNOWN : INSTR_PIC;
    }
    case 31: {
        // mtspr / mfspr on CTR: the stub owns CTR from its mtctr to its bctr.
        const unsigned xo = (word >> 1) & 0x3FFu;
        if ((xo == 467u || xo == 339u) && sprField(word) == 9u)
            return INSTR_UNKNOWN;
        return INSTR_PIC;
    }
    case 0:    // reserved -- an all-zero word is not an instruction
    case 1:    // reserved
    case 17:   // sc
        return INSTR_UNKNOWN;
    default:
        return INSTR_PIC;
    }
}

uint32_t Lis(unsigned reg, uint32_t address)
{
    return 0x3C000000u | ((reg & 31u) << 21) | (address >> 16);
}

uint32_t Ori(unsigned reg, uint32_t address)
{
    return 0x60000000u | ((reg & 31u) << 21) | ((reg & 31u) << 16) |
           (address & 0xFFFFu);
}

// mtspr: opcode 31, extended 467, SPR field with its halves swapped.
uint32_t MtCtr(unsigned reg) { return 0x7C0903A6u | ((reg & 31u) << 21); }
uint32_t MtLr(unsigned reg)  { return 0x7C0803A6u | ((reg & 31u) << 21); }

uint32_t Bctr() { return 0x4E800420u; }
uint32_t Blr()  { return 0x4E800020u; }
uint32_t Nop()  { return 0x60000000u; }

uint32_t Lfs(unsigned frt, unsigned ra, int16_t disp)
{
    return 0xC0000000u | ((frt & 31u) << 21) | ((ra & 31u) << 16) |
           ((uint32_t)disp & 0xFFFFu);
}

uint32_t Lwz(unsigned rt, unsigned ra, int16_t disp)
{
    return 0x80000000u | ((rt & 31u) << 21) | ((ra & 31u) << 16) |
           ((uint32_t)disp & 0xFFFFu);
}

bool EncodeBranch(uint32_t from, uint32_t to, bool link, uint32_t* out)
{
    if (!out || ((from | to) & 3u))
        return false;
    const int32_t distance = (int32_t)(to - from);
    if (distance < -0x02000000 || distance > 0x01FFFFFC)
        return false;
    *out = 0x48000000u | ((uint32_t)distance & 0x03FFFFFCu) | (link ? 1u : 0u);
    return true;
}

bool EncodeAbsoluteBranch(uint32_t to, bool link, uint32_t* out)
{
    if (!out || (to & 3u) || to > 0x01FFFFFCu)
        return false;
    *out = 0x48000002u | (to & 0x03FFFFFCu) | (link ? 1u : 0u);
    return true;
}

} // namespace Ppc
} // namespace WuPatch
