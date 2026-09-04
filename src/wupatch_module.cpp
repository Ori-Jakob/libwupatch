#include "libwupatch/wupatch_module.h"

#include <coreinit/dynload.h>
#include <string.h>

namespace WuPatch {
namespace Module {

static const int kMaxModules = 48;

bool Find(const char* suffix, uint32_t* textAddr, uint32_t* textSize)
{
    if (!suffix || !suffix[0] || !textAddr || !textSize)
        return false;

    const int count = OSDynLoad_GetNumberOfRPLs();
    if (count <= 0)
        return false;

    OSDynLoad_NotifyData infos[kMaxModules];
    const int wanted = count > kMaxModules ? kMaxModules : count;
    if (!OSDynLoad_GetRPLInfo(0, (uint32_t)wanted, infos))
        return false;

    const size_t suffixLen = strlen(suffix);
    for (int i = 0; i < wanted; ++i) {
        const char* name = infos[i].name;
        if (!name)
            continue;
        const size_t nameLen = strlen(name);
        if (nameLen < suffixLen ||
            strcmp(name + nameLen - suffixLen, suffix) != 0)
            continue;
        *textAddr = infos[i].textAddr;
        *textSize = infos[i].textSize;
        return true;
    }
    return false;
}

int CountUnnamed()
{
    const int count = OSDynLoad_GetNumberOfRPLs();
    if (count <= 0)
        return 0;
    OSDynLoad_NotifyData infos[kMaxModules];
    const int wanted = count > kMaxModules ? kMaxModules : count;
    if (!OSDynLoad_GetRPLInfo(0, (uint32_t)wanted, infos))
        return 0;
    int unnamed = 0;
    for (int i = 0; i < wanted; ++i)
        if (!infos[i].name)
            ++unnamed;
    return unnamed;
}

} // namespace Module
} // namespace WuPatch
