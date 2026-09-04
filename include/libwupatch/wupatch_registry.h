#pragma once

#include <stdint.h>

#include "libwupatch/wupatch_config.h"
#include "libwupatch/wupatch_types.h"

namespace WuPatch {

// Idempotent. Calling again with different deltas removes anything applied.
void Configure(const Config& cfg);
bool IsConfigured();

// A new process: forget every handle, shim and stated intent.
void OnApplicationStart();

// Must run while the title is still alive: removal writes to its text.
int  RemoveAll();

void OnApplicationEnd();

// Registration only; never validates against live memory. The owner is copied.
Handle Declare(const Desc& desc);

// Refused while the patch is wanted or applied. The row is not reused this process.
bool Undeclare(Handle h);

// Intent, not action. Edge-triggered.
void  SetEnabled(Handle h, bool enabled);
bool  IsEnabled(Handle h);
State GetState(Handle h);

// The next hook in the chain, or the displaced word then site+4. 0 until applied.
uint32_t GetOriginalThunk(Handle h);

// The only place this library writes. Once per frame, AFTER intent is set.
void Tick();

const char* StateName(State state);
const char* ShapeName(Shape shape);
const char* BackendName();

int         Count();               // rows ever used; some may be unused
const char* OwnerAt(int index);
State       StateAt(int index);
uint32_t    LinkAddrAt(int index);
uint32_t    RuntimeAddrAt(int index);

// 1-based position and chain length; both 0 when the patch is not applied.
int ChainPosition(Handle h);
int ChainLength(Handle h);

int  CollisionCount();
bool GetCollision(int index, const char** ownerA, const char** ownerB, uint32_t* linkAddr);

void LogState();

} // namespace WuPatch
