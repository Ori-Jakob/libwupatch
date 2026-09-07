#include "libwupatch/wupatch_shim.h"

#include "libwupatch/wupatch_ppc.h"

namespace WuPatch {
namespace Shim {

int BuildCall(uint32_t* out, uint32_t ret, uint32_t hook)
{
    out[0] = Ppc::Lis(11, ret);
    out[1] = Ppc::Ori(11, ret);
    out[2] = Ppc::MtLr(11);
    out[3] = Ppc::Lis(11, hook);
    out[4] = Ppc::Ori(11, hook);
    out[5] = Ppc::MtCtr(11);
    out[6] = Ppc::Bctr();
    return kCallWords;
}

int BuildRewrite(uint32_t* out, uint32_t ret, uint32_t replacement)
{
    out[0] = Ppc::Stwu(1, 1, -16);
    out[1] = Ppc::Stw(11, 1, 8);
    out[2] = Ppc::Lis(11, ret);
    out[3] = Ppc::Ori(11, ret);
    out[4] = Ppc::MtCtr(11);
    out[5] = Ppc::Lwz(11, 1, 8);
    out[6] = Ppc::Addi(1, 1, 16);
    out[7] = replacement;
    out[8] = Ppc::Bctr();
    return kRewriteWords;
}

int BuildIndirect(uint32_t* out, uint32_t ptrAddr)
{
    out[0] = Ppc::Stwu(1, 1, -16);
    out[1] = Ppc::Stw(11, 1, 8);
    out[2] = Ppc::Lis(11, ptrAddr);
    out[3] = Ppc::Ori(11, ptrAddr);
    out[4] = Ppc::Lwz(11, 11, 0);
    out[5] = Ppc::MtCtr(11);
    out[6] = Ppc::Lwz(11, 1, 8);
    out[7] = Ppc::Addi(1, 1, 16);
    out[8] = Ppc::Bctr();
    return kIndirectWords;
}

} // namespace Shim
} // namespace WuPatch
