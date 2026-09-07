#pragma once

#include <stdint.h>

namespace WuPatch {
namespace Ppc {

unsigned PrimaryOpcode(uint32_t word);

enum InstrClass {
    INSTR_PIC = 0,       // safe to execute from a stub at another address
    INSTR_PC_RELATIVE,   // a relative branch; would resolve against the stub
    INSTR_UNKNOWN,       // not a form this library will vouch for
};

// The stub owns CTR, so a word that touches CTR is refused along with the rest.
InstrClass Classify(uint32_t word);

// Both halves are taken PLAIN: ori is unsigned, so no +0x8000 adjustment.
uint32_t Lis(unsigned reg, uint32_t address);
uint32_t Ori(unsigned reg, uint32_t address);

uint32_t MtCtr(unsigned reg);
uint32_t MtLr(unsigned reg);
uint32_t Bctr();
uint32_t Blr();
uint32_t Nop();
uint32_t Lfs(unsigned frt, unsigned ra, int16_t disp);
uint32_t Lwz(unsigned rt, unsigned ra, int16_t disp);
uint32_t Stw(unsigned rs, unsigned ra, int16_t disp);
uint32_t Stwu(unsigned rs, unsigned ra, int16_t disp);
uint32_t Addi(unsigned rt, unsigned ra, int16_t imm);

// False when either address is unaligned or the distance exceeds +-32 MB.
bool EncodeBranch(uint32_t from, uint32_t to, bool link, uint32_t* out);

// False unless `to` is aligned and below 0x02000000.
bool EncodeAbsoluteBranch(uint32_t to, bool link, uint32_t* out);

} // namespace Ppc
} // namespace WuPatch
