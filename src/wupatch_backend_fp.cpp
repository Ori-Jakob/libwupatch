#if defined(__WUPS__) || defined(WUPATCH_HAVE_FUNCTION_PATCHER)

#include "libwupatch/wupatch_backend.h"

#include "libwupatch/wupatch_log.h"
#include "libwupatch/wupatch_module.h"

#include <coreinit/debug.h>
#include <coreinit/memorymap.h>
#include <function_patcher/function_patching.h>
#include <string.h>

namespace WuPatch {

static bool s_libReady = false;

static bool fpInit(const Config&)
{
    const FunctionPatcherStatus status = FunctionPatcher_InitLibrary();
    s_libReady = status == FUNCTION_PATCHER_RESULT_SUCCESS;
    if (!s_libReady)
        Log::Warn("FunctionPatcher unavailable: %s", FunctionPatcher_GetStatusStr(status));
    return s_libReady;
}

static bool fpApply(SiteRecord& s, const Config& cfg, uint32_t moduleTextAddr, const char* owner)
{
    if (!s_libReady)
        return false;

    function_replacement_data_t data;
    memset(&data, 0, sizeof(data));
    data.version       = FUNCTION_REPLACEMENT_DATA_STRUCT_VERSION;
    data.replaceAddr   = s.entryAddr;
    data.replaceCall   = &s.fpThunk;
    // Any other target process adds a cmpwi to the patcher's stub, clobbering CR0.
    data.targetProcess = FP_TARGET_PROCESS_ALL;

    if (s.external) {
        // A library loads in every process but the hook lives in one; gating costs CR0.
        data.targetProcess = (FunctionPatcherTargetProcess)OSGetUPID();
        data.type = FUNCTION_PATCHER_REPLACE_BY_LIB_OR_ADDRESS;
        if (s.library == kLibraryByAddress) {
            const uint32_t physical = OSEffectiveToPhysical(s.runtimeAddr);
            if (!physical) {
                Log::Error("%s: %08X has no physical address", owner, (unsigned)s.runtimeAddr);
                return false;
            }
            data.physicalAddr = physical;
            data.virtualAddr  = s.runtimeAddr;
            data.ReplaceInRPL.library       = LIBRARY_OTHER;
            data.ReplaceInRPL.function_name = s.functionName;
        } else {
            data.ReplaceInRPL.library =
                (function_replacement_library_type_t)s.library;
            data.ReplaceInRPL.function_name = s.functionName;
        }
    } else if (cfg.fpByPhysicalAddress) {
        // The module gates nothing here and re-applies in every later process.
        const uint32_t physical = OSEffectiveToPhysical(s.runtimeAddr);
        if (!physical) {
            Log::Error("%s: %08X has no physical address", owner, (unsigned)s.runtimeAddr);
            return false;
        }
        data.type         = FUNCTION_PATCHER_REPLACE_BY_LIB_OR_ADDRESS;
        data.physicalAddr = physical;
        data.virtualAddr  = s.runtimeAddr;
        data.ReplaceInRPL.library       = LIBRARY_OTHER;
        data.ReplaceInRPL.function_name = "";
    } else {
        // The module walks the module list with string_view and crashes on a NULL name.
        const int unnamed = Module::CountUnnamed();
        if (unnamed) {
            Log::Error("%s: %d loaded module(s) have no name; the FunctionPatcher "
                       "module would crash resolving '%s' -- refused (see "
                       "Config::fpByPhysicalAddress)", owner, unnamed, cfg.executableName);
            return false;
        }
        data.type = FUNCTION_PATCHER_REPLACE_FOR_EXECUTABLE_BY_ADDRESS;
        data.ReplaceInRPX.targetTitleIds      = cfg.titleIds;
        data.ReplaceInRPX.targetTitleIdsCount = cfg.titleIdCount;
        data.ReplaceInRPX.versionMin          = cfg.versionMin;
        data.ReplaceInRPX.versionMax          = cfg.versionMax;
        data.ReplaceInRPX.executableName      = cfg.executableName;
        data.ReplaceInRPX.textOffset          = s.runtimeAddr - moduleTextAddr;
        data.ReplaceInRPX.functionName        = 0;
    }

    PatchedFunctionHandle handle = 0;
    bool applied = false;
    const FunctionPatcherStatus status =
        FunctionPatcher_AddFunctionPatch(&data, &handle, &applied);
    if (status != FUNCTION_PATCHER_RESULT_SUCCESS) {
        Log::Error("%s: AddFunctionPatch(%08X) failed: %s", owner,
                   (unsigned)s.runtimeAddr, FunctionPatcher_GetStatusStr(status));
        return false;
    }
    if (!applied) {
        // A deferred entry would be applied on a later boot with nothing to remove it.
        FunctionPatcher_RemoveFunctionPatch(handle);
        Log::Error("%s: patcher deferred the patch -- title ID / version gate "
                   "does not match the running title", owner);
        return false;
    }

    s.fpHandle = handle;
    return true;
}

static bool fpRemove(SiteRecord& s, const Config&, const char* owner)
{
    if (!s.fpHandle)
        return true;
    const FunctionPatcherStatus status = FunctionPatcher_RemoveFunctionPatch(s.fpHandle);
    if (status != FUNCTION_PATCHER_RESULT_SUCCESS) {
        Log::Warn("%s: RemoveFunctionPatch(%08X) failed: %s", owner,
                  (unsigned)s.runtimeAddr, FunctionPatcher_GetStatusStr(status));
        return false;
    }
    s.fpHandle = 0;
    return true;
}

static void fpForget(SiteRecord& s)
{
    s.fpHandle = 0;
    s_libReady = false;
}

static const BackendOps s_ops = { "FunctionPatcher", fpInit, fpApply, fpRemove, fpForget };

const BackendOps* FunctionPatcherBackend() { return &s_ops; }

} // namespace WuPatch

#else

#include "libwupatch/wupatch_backend.h"

namespace WuPatch {
const BackendOps* FunctionPatcherBackend() { return 0; }
} // namespace WuPatch

#endif
