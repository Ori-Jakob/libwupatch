# libwupatch

A game-agnostic library for installing single-instruction patches into a Wii U
title from a plugin, an emulator module or a codecave loader.

It takes **link-time** addresses and relocates them itself, verifies each site
against the RPX image in every form the loader can rewrite it into, refuses to
write until the whole wanted set validates, and — on Aroma — never writes to the
title's text at all. `docs/DESIGN.md` says why each of those is a rule.

Nothing in here knows a title ID, an executable name or an address. The host
supplies all of that through one struct.

## Dropping it into a project

Two Makefile lines:

```make
SOURCES  += extern/libwupatch/src
INCLUDES += extern/libwupatch/include
```

On Aroma, link `-lfunctionpatcher` (WUMS). The direct-write backend needs only
`libwut`.

One include:

```cpp
#include "libwupatch/wupatch.h"
```

One configuration, once both segment deltas are known:

```cpp
static const uint64_t kTitleIds[] = { 0x0005000010143500ull, /* ... */ };

WuPatch::Config cfg = {};
cfg.titleIds       = kTitleIds;          // the patcher's own gate
cfg.titleIdCount   = 3;
cfg.executableName = ".rpx";             // suffix-matched against loaded modules
cfg.versionMin     = 0;
cfg.versionMax     = 0xFFFF;
cfg.tag            = "[mytool][patch]";  // log prefix
cfg.textDelta      = textDelta;  cfg.textResolved = true;   // loader's textOffset
cfg.dataDelta      = dataDelta;  cfg.dataResolved = true;   // loader's dataOffset
cfg.backend        = WuPatch::BACKEND_AUTO;
WuPatch::SetLogSink(myLogSink);          // void (*)(int level, const char* line)
WuPatch::Configure(cfg);
```

Four lifecycle calls:

| when | call |
|---|---|
| a new process starts | `WuPatch::OnApplicationStart()` |
| once per frame, **after** the features that set intent | `WuPatch::Tick()` |
| the title is about to exit, while it is still alive | `WuPatch::RemoveAll()` |
| the process is gone | `WuPatch::OnApplicationEnd()` |

`RemoveAll()` is not optional on Aroma. The FunctionPatcher keeps every patch
across title switches and re-applies it on the next application start; a
leaked handle is a patch you can no longer remove.

`OnApplicationStart()` also clears every feature's stated intent. A feature
asks again from its own tick in the new process; nothing is re-applied on the
strength of what the last process wanted.

## Using it

Three verbs. Only the last one writes.

```cpp
WuPatch::Desc d = {};
d.owner         = "Auto Wind";       // log prefix, collision attribution
d.site.linkAddr = site->addr;        // LINK-TIME address; the library relocates it
d.site.expected = site->word;        // the word the RPX image holds there
d.shape         = WuPatch::SHAPE_CALL;
d.hook          = (const void*)myHook;
WuPatch::Handle h = WuPatch::Declare(d);   // registration only; never validates

WuPatch::SetEnabled(h, wanted);      // intent; edge-triggered, call every frame if you like
// ... WuPatch::Tick() reconciles, later this frame
```

`GetState(h)` reports `declared / blocked / refused / collided / applied /
failed`, and every refusal is logged once with the live word, the expected word
and the data delta.

`Undeclare(h)` retires a patch that is neither wanted nor applied. The owner
string is copied at `Declare`, so a feature whose strings go away (a module
that gets unloaded) leaves nothing dangling.

### Several patches on one site

Two `SHAPE_JUMP` patches at one address, or two `SHAPE_CALL` patches, do not
collide: they **chain**. The site is patched once, with a dispatcher that jumps
to the first hook; each hook's `GetOriginalThunk(h)` leads to the next hook,
and the last one's to the displaced instruction. `d.priority` orders the chain
(higher runs earlier, ties in the order they were applied); `d.flags =
FLAG_EXCLUSIVE` opts a patch out, and anything that cannot join an occupied
site — another shape, a `REWRITE`, an exclusive patch — is a collision as
before. `ChainPosition(h)` and `ChainLength(h)` say where a patch landed.

A hook that does not branch or call on to its original thunk ends the chain
there, exactly as a WUPS function replacement that never calls `real_X` does.

The tables hold `WUPATCH_MAX_PATCHES` (128) patches and `WUPATCH_MAX_SITES`
(96) sites; define either before including the headers to change it.

### Shapes and the clobber contract

| shape | what happens at the site | clobbered on entry | the hook must |
|---|---|---|---|
| `SHAPE_JUMP` | branch to `hook`, LR untouched | r11, CTR | never return — unless the site is a function's first instruction, where LR is the game's return address and a C function with the game function's signature may simply return |
| `SHAPE_CALL` | `hook` entered as if by `bl`: LR = site+4 | r11, CTR, LR | end in `blr` or branch to `GetOriginalThunk(h)`; preserve everything else **including CR** |
| `SHAPE_REWRITE` | run `replacement` instead of the site's word, continue at site+4 | r11, CTR | replacement must not **read** r11 and must be position-independent |

`GetOriginalThunk(h)` is "run what would have run at the site": the next hook
in the chain, or a stub that re-executes the displaced instruction and
continues at site+4. A `CALL` hook does its work and then `b`s there; a `JUMP`
hook at a function entry calls it as the original function. The address is
fixed for the life of the process, so a hook may cache it. It is `0` while the
patch is not applied, and `0` for a displaced word that cannot run from a stub
— a relative branch, or anything that touches CTR — in which case the site
takes one patch and never a chain.

### Data swaps

For a pointer or a table of slots in `.data` — a camera mode table, a vtable
entry — there is `WuPatch::Data::Declare(SwapDesc)`. Not a code patch, but it
shares the collision model and the restore rule: only a slot still holding
*our* value is put back.

## Backends

| `cfg.backend` | writes code via | for |
|---|---|---|
| `BACKEND_FUNCTION_PATCHER` | Aroma's FunctionPatcher module | Aroma plugins |
| `BACKEND_DIRECT` | `cfg.writeFn` if set, else a plain store | emulators, codecaves, kernel-capable hosts |
| `BACKEND_AUTO` | FunctionPatcher if it answers; else direct **only if `cfg.allowDirectWrite`** | the default |

Direct write is never selected on its own. If there is no patcher module and the
host has not said the memory is writable, nothing is ever applied and every
patch reports `blocked`. The direct backend also refuses any branch it cannot
encode in range — it never bridges the gap with a stub inside the title.

The FunctionPatcher backend submits a site as an executable-by-address patch,
which the module resolves by walking the loaded-module list — and it crashes
in `strlen` on a module the loader lists without a name, which an RPL loaded
from the SD card can be. The backend counts those first and refuses rather
than let it. `cfg.fpByPhysicalAddress` is the way around: the site is
submitted by physical and effective address, resolved and gated by nobody,
and re-applied by the module in every later process, so it is opt-in and it
makes `RemoveAll()` on every exit path mandatory rather than merely important.

Data swaps need no backend at all. They are plain stores into `.data` and
proceed on the data delta alone, so a runtime with no code backend still gets
its table swaps.
