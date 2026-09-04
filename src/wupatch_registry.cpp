#include "libwupatch/wupatch_registry.h"

#include "libwupatch/wupatch_apply.h"
#include "libwupatch/wupatch_backend.h"
#include "libwupatch/wupatch_chain.h"
#include "libwupatch/wupatch_data_internal.h"
#include "libwupatch/wupatch_log.h"
#include "libwupatch/wupatch_overlap.h"
#include "libwupatch/wupatch_ppc.h"

#include <string.h>

namespace WuPatch {

static const uint8_t kKindPatch = 0;
static const uint8_t kKindData  = 1;

static Config      s_cfg;
static bool        s_configured = false;
static PatchRecord s_records[kMaxPatches];
static Chain::Link s_links[kMaxPatches];
static int         s_count = 0;          // high-water mark of rows ever used
static SiteRecord  s_sites[kMaxSites];
static int         s_siteCount = 0;
static bool        s_dirty = false;
static uint32_t    s_tick = 0;

static Overlap::Collision s_collisions[Overlap::kMaxCollisions];
static int                s_collisionCount = 0;

static PatchRecord* at(Handle h)
{
    const int i = (int)h - 1;
    return (i >= 0 && i < s_count && s_records[i].used) ? &s_records[i] : 0;
}

static SiteRecord* siteOf(const PatchRecord& r)
{
    return r.site >= 0 ? &s_sites[r.site] : 0;
}

static const char* kindOwner(uint8_t kind, uint8_t index)
{
    return kind == kKindPatch ? s_records[index].desc.owner : Data::OwnerAt(index);
}

static uint32_t kindLinkAddr(uint8_t kind, uint8_t index)
{
    return kind == kKindPatch ? s_records[index].desc.site.linkAddr : Data::LinkAddrAt(index);
}

static bool chainable(const Desc& d)
{
    return d.shape != SHAPE_REWRITE && !(d.flags & FLAG_EXCLUSIVE);
}

static bool compatible(const Desc& a, const Desc& b)
{
    return a.shape == b.shape && chainable(a) && chainable(b);
}

static void resetLinkState(PatchRecord& r)
{
    r.state = STATE_DECLARED;
    r.site = -1;
    r.nextTarget = 0;
    Apply::ForgetLink(r);
    Chain::InitLink(s_links[r.index], &r.nextTarget);
}

static void resetSiteTable()
{
    for (int i = 0; i < s_siteCount; ++i) {
        if (s_sites[i].used)
            Apply::ForgetSite(s_sites[i]);
        s_sites[i].used = false;
    }
    s_siteCount = 0;
}

// Intent is dropped with the handles: nothing is re-applied on an old request.
static void forgetProcess(bool keepIntent)
{
    for (int i = 0; i < s_count; ++i) {
        PatchRecord& r = s_records[i];
        if (r.used) {
            resetLinkState(r);
            if (!keepIntent)
                r.wanted = false;
        }
        // A retired row's slot belonged to the old process; it is free again.
        r.retired = false;
    }
    resetSiteTable();
    if (!keepIntent)
        Data::Forget();
    Apply::Reset();
    s_collisionCount = 0;
    s_dirty = true;
}

void Configure(const Config& cfg)
{
    if (s_configured &&
        (cfg.textDelta != s_cfg.textDelta || cfg.dataDelta != s_cfg.dataDelta)) {
        // Every runtime address just moved, and every shim carries an old one.
        RemoveAll();
        forgetProcess(true);
    }
    s_cfg = cfg;
    s_configured = true;
    s_dirty = true;
    Log::SetTag(cfg.tag);
    Apply::Reset();
}

bool IsConfigured() { return s_configured; }

void OnApplicationStart()
{
    forgetProcess(false);
}

int RemoveAll()
{
    int removed = 0;
    for (int i = 0; i < s_siteCount; ++i) {
        SiteRecord& s = s_sites[i];
        if (!s.used || s.state != STATE_APPLIED)
            continue;
        const int first = s.chain.firstLink;
        const char* owner = first >= 0 ? s_records[first].desc.owner : "?";
        // For a REWRITE site the owner is the one record bound to it.
        if (s.shape == SHAPE_REWRITE)
            for (int k = 0; k < s_count; ++k)
                if (s_records[k].used && s_records[k].site == i && s_records[k].state == STATE_APPLIED)
                    owner = s_records[k].desc.owner;
        const bool ok = Apply::UninstallSite(s, s_cfg, owner);
        for (int k = 0; k < s_count; ++k) {
            PatchRecord& r = s_records[k];
            if (r.used && r.site == i && r.state == STATE_APPLIED) {
                r.state = ok ? STATE_DECLARED : STATE_FAILED;
                if (ok)
                    ++removed;
            }
        }
        // The site is restored; a core still in the chain follows words left intact.
        Chain::Clear(s.chain, s_links);
    }
    removed += Data::RemoveAll();
    if (removed)
        Log::Info("removed %d patch(es)", removed);
    return removed;
}

void OnApplicationEnd()
{
    forgetProcess(false);
}

static bool sameDesc(const Desc& a, const Desc& b)
{
    return a.site.linkAddr == b.site.linkAddr && a.shape == b.shape &&
           a.hook == b.hook && a.replacement == b.replacement;
}

Handle Declare(const Desc& desc)
{
    const char* owner = desc.owner ? desc.owner : "?";
    if (!desc.owner || !desc.site.linkAddr || (desc.site.linkAddr & 3u)) {
        Log::Error("'%s' rejected: bad site %08X", owner, (unsigned)desc.site.linkAddr);
        return kInvalidHandle;
    }
    if (desc.shape < SHAPE_JUMP || desc.shape > SHAPE_REWRITE) {
        Log::Error("'%s' rejected: bad shape", owner);
        return kInvalidHandle;
    }
    if (desc.shape != SHAPE_REWRITE &&
        (!desc.hook || ((uintptr_t)desc.hook & 3u))) {
        Log::Error("'%s' rejected: bad hook %p", owner, desc.hook);
        return kInvalidHandle;
    }
    if (desc.external && (desc.shape != SHAPE_JUMP || !desc.functionName)) {
        // A host-resolved export can only be said to be a function entry.
        Log::Error("'%s' rejected: an export must be a named JUMP", owner);
        return kInvalidHandle;
    }
    if (desc.shape == SHAPE_REWRITE &&
        Ppc::Classify(desc.replacement) != Ppc::INSTR_PIC) {
        // The replacement runs from a stub, not from the site.
        Log::Error("'%s' rejected: replacement %08X is not position-independent",
                   owner, (unsigned)desc.replacement);
        return kInvalidHandle;
    }

    for (int i = 0; i < s_count; ++i)
        if (s_records[i].used && sameDesc(s_records[i].desc, desc))
            return (Handle)(i + 1);

    int idx = -1;
    for (int i = 0; i < s_count; ++i) {
        if (!s_records[i].used && !s_records[i].retired) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (s_count >= kMaxPatches) {
            Log::Error("'%s' rejected: table full (%d)", owner, kMaxPatches);
            return kInvalidHandle;
        }
        idx = s_count++;
    }

    PatchRecord& r = s_records[idx];
    r.desc = desc;
    strncpy(r.ownerBuf, owner, kOwnerChars - 1);
    r.ownerBuf[kOwnerChars - 1] = '\0';
    r.desc.owner = r.ownerBuf;
    r.index = idx;
    r.used = true;
    r.retired = false;
    r.wanted = false;
    resetLinkState(r);
    return (Handle)(idx + 1);
}

bool Undeclare(Handle h)
{
    PatchRecord* r = at(h);
    if (!r)
        return false;
    if (r->wanted || r->state == STATE_APPLIED || s_links[r->index].attached) {
        Log::Warn("%s: cannot undeclare while wanted or applied", r->desc.owner);
        return false;
    }
    r->used = false;
    r->wanted = false;
    r->state = STATE_DECLARED;
    r->retired = r->shimCommitted;
    r->site = -1;
    s_dirty = true;
    return true;
}

void SetEnabled(Handle h, bool enabled)
{
    PatchRecord* r = at(h);
    if (!r || r->wanted == enabled)
        return;
    r->wanted = enabled;
    r->nextTryTick = 0;   // a fresh request is tried at once, not on the retry clock
    s_dirty = true;
}

bool IsEnabled(Handle h)
{
    const PatchRecord* r = at(h);
    return r && r->wanted;
}

State GetState(Handle h)
{
    const PatchRecord* r = at(h);
    return r ? r->state : STATE_DECLARED;
}

uint32_t GetOriginalThunk(Handle h)
{
    const PatchRecord* r = at(h);
    if (!r || r->state != STATE_APPLIED)
        return 0;
    const SiteRecord* s = siteOf(*r);
    if (!s || s->shape == SHAPE_REWRITE || !s->originalThunk)
        return 0;
    return r->nextShim;
}

// A committed slot serves one shape, so a patch needing other words gets its own record.
static SiteRecord* bindSite(PatchRecord& r)
{
    const Desc& d = r.desc;
    for (int i = 0; i < s_siteCount; ++i) {
        SiteRecord& s = s_sites[i];
        if (!s.used || s.linkAddr != d.site.linkAddr || s.external != d.external)
            continue;
        if (s.committed) {
            if (s.shape != d.shape)
                continue;
            if (d.shape == SHAPE_REWRITE && s.replacement != d.replacement)
                continue;
        } else if (s.state != STATE_APPLIED && s.chain.firstLink == -1) {
            // Not yet committed and empty: it takes this patch's shape.
            s.shape = d.shape;
            s.replacement = d.replacement;
        } else if (s.shape != d.shape) {
            continue;
        }
        r.site = i;
        return &s;
    }

    if (s_siteCount >= kMaxSites) {
        Log::Error("'%s' rejected: site table full (%d)", d.owner, kMaxSites);
        return 0;
    }
    SiteRecord& s = s_sites[s_siteCount];
    memset((void*)&s, 0, sizeof(s));
    s.used = true;
    s.index = s_siteCount;
    s.linkAddr = d.site.linkAddr;
    s.expected = d.external ? 0u : d.site.expected;
    s.external = d.external;
    s.library = d.library;
    if (d.external && d.functionName) {
        strncpy(s.functionName, d.functionName, kFunctionNameChars - 1);
        s.functionName[kFunctionNameChars - 1] = 0;
    }
    s.shape = d.shape;
    s.replacement = d.replacement;
    s.state = STATE_DECLARED;
    Chain::Init(s.chain, &s.head, 0);
    r.site = s_siteCount++;
    return &s;
}

static void attachLink(PatchRecord& r, SiteRecord& s)
{
    Chain::Link& l = s_links[r.index];
    l.priority = r.desc.priority;
    l.entry = s.shape == SHAPE_CALL ? r.callShim : (uint32_t)(uintptr_t)r.desc.hook;
    l.nextTarget = &r.nextTarget;
    Chain::Attach(s.chain, s_links, r.index, Apply::Publish);
}

// An applied link leaves; a site with nothing left is restored.
static void removeLink(PatchRecord& r)
{
    SiteRecord* s = siteOf(r);
    if (!s) {
        r.state = STATE_DECLARED;
        return;
    }
    if (s->shape != SHAPE_REWRITE)
        Chain::Detach(s->chain, s_links, r.index, Apply::Publish);
    r.state = STATE_DECLARED;

    if (s->shape == SHAPE_REWRITE || s->chain.firstLink == -1) {
        if (!Apply::UninstallSite(*s, s_cfg, r.desc.owner))
            r.state = STATE_FAILED;
    } else {
        Log::Info("%s: left the chain at %08X, %d remain", r.desc.owner,
                  (unsigned)s->runtimeAddr, Chain::Length(s->chain, s_links));
    }
}

static void blockWanted()
{
    for (int i = 0; i < s_count; ++i) {
        PatchRecord& r = s_records[i];
        if (r.used && r.wanted && r.state != STATE_APPLIED)
            r.state = STATE_BLOCKED;
    }
    Data::Reconcile(s_cfg, false);
}

static void markCollided(uint8_t kind, uint8_t index)
{
    if (kind == kKindPatch) {
        if (s_records[index].state != STATE_APPLIED)
            s_records[index].state = STATE_COLLIDED;
    } else {
        Data::MarkCollided(index);
    }
}

// Patches that can chain share one extent; two that cannot are both refused.
static void findCollisions()
{
    Overlap::Extent extents[kMaxPatches + Data::kMaxSwaps];
    int n = 0;
    bool emitted[kMaxPatches];
    for (int i = 0; i < s_count; ++i) {
        PatchRecord& r = s_records[i];
        emitted[i] = false;
        if (r.used && (r.wanted || r.state == STATE_APPLIED) && r.state == STATE_COLLIDED)
            r.state = STATE_DECLARED;
    }

    for (int i = 0; i < s_count; ++i) {
        PatchRecord& r = s_records[i];
        if (!r.used || (!r.wanted && r.state != STATE_APPLIED) || emitted[i])
            continue;

        // Gather the group at this address.
        int applied = -1;
        bool appliedExclusive = false;
        bool allCompatible = true;
        for (int k = 0; k < s_count; ++k) {
            const PatchRecord& o = s_records[k];
            if (!o.used || (!o.wanted && o.state != STATE_APPLIED) ||
                o.desc.site.linkAddr != r.desc.site.linkAddr)
                continue;
            if (o.state == STATE_APPLIED) {
                if (applied < 0)
                    applied = k;
                if (!chainable(o.desc))
                    appliedExclusive = true;
            }
            if (k != i && !compatible(o.desc, r.desc))
                allCompatible = false;
            for (int m = 0; m < k; ++m) {
                const PatchRecord& p = s_records[m];
                if (p.used && (p.wanted || p.state == STATE_APPLIED) &&
                    p.desc.site.linkAddr == r.desc.site.linkAddr && !compatible(o.desc, p.desc))
                    allCompatible = false;
            }
        }

        // One extent per member that stands alone; one for those that share.
        for (int k = 0; k < s_count; ++k) {
            PatchRecord& o = s_records[k];
            if (!o.used || (!o.wanted && o.state != STATE_APPLIED) ||
                o.desc.site.linkAddr != r.desc.site.linkAddr || emitted[k])
                continue;
            emitted[k] = true;
            bool shares;
            if (applied >= 0)
                shares = o.state == STATE_APPLIED ||
                         (!appliedExclusive && compatible(o.desc, s_records[applied].desc));
            else
                shares = allCompatible;
            if (shares && k != (applied >= 0 ? applied : i))
                continue;   // represented by the group's extent
            Overlap::Extent& e = extents[n++];
            e.start   = o.desc.site.linkAddr;
            e.end     = o.desc.site.linkAddr + 4u;
            e.segment = SEG_TEXT;
            e.kind    = kKindPatch;
            e.index   = (uint8_t)k;
            e.applied = o.state == STATE_APPLIED;
        }
    }
    n += Data::CollectExtents(extents + n, Data::kMaxSwaps, kKindData);

    const int previous = s_collisionCount;
    s_collisionCount = Overlap::Find(extents, n, s_collisions, Overlap::kMaxCollisions);

    for (int i = 0; i < s_collisionCount; ++i) {
        const Overlap::Collision& c = s_collisions[i];
        bool aApplied = false;
        for (int k = 0; k < n; ++k)
            if (extents[k].kind == c.kindA && extents[k].index == c.indexA)
                aApplied = extents[k].applied;
        markCollided(c.kindB, c.indexB);
        if (!aApplied)
            markCollided(c.kindA, c.indexA);
        if (i >= previous)
            Log::Warn("collision at %08X: '%s' and '%s'", (unsigned)c.start,
                      kindOwner(c.kindA, c.indexA), kindOwner(c.kindB, c.indexB));
    }
}

// Returns false when the batch must be rolled back.
static bool applyOne(PatchRecord& r)
{
    SiteRecord* s = bindSite(r);
    if (!s) {
        r.state = STATE_FAILED;
        return true;
    }

    if (s->state == STATE_APPLIED) {
        // Join an installed site.
        if (s->shape == SHAPE_REWRITE || !chainable(r.desc)) {
            r.state = STATE_COLLIDED;
            return true;
        }
        if (!s->originalThunk) {
            r.state = STATE_COLLIDED;
            r.nextTryTick = s_tick + Apply::kRetryTicks;
            Log::Warn("%s: %08X cannot take a chain -- its displaced word %08X "
                      "cannot run from a stub", r.desc.owner, (unsigned)s->runtimeAddr,
                      (unsigned)s->liveWord);
            return true;
        }
        // An export has no declared word to disagree about
        if (!s->external && r.desc.site.expected != s->expected) {
            r.state = STATE_REFUSED;
            r.nextTryTick = s_tick + Apply::kRetryTicks;
            Log::Warn("%s: expects %08X at %08X but the site was installed against %08X",
                      r.desc.owner, (unsigned)r.desc.site.expected,
                      (unsigned)s->runtimeAddr, (unsigned)s->expected);
            return true;
        }
        if (!Apply::PrepareLink(r, *s)) {
            r.state = STATE_FAILED;
            return false;
        }
        attachLink(r, *s);
        r.state = STATE_APPLIED;
        Log::Info("%s: joined the chain at %08X, position %d of %d", r.desc.owner,
                  (unsigned)s->runtimeAddr, Chain::Position(s->chain, s_links, r.index),
                  Chain::Length(s->chain, s_links));
        return true;
    }

    // Install the site for this patch.
    s->expected = r.desc.external ? 0u : r.desc.site.expected;
    if (!s->committed) {
        s->shape = r.desc.shape;
        s->replacement = r.desc.replacement;
    }
    if (!Apply::GuardSite(*s, s_cfg, r.desc.owner)) {
        r.state = STATE_REFUSED;
        r.nextTryTick = s_tick + Apply::kRetryTicks;
        return true;
    }
    if (!Apply::PrepareSite(*s, r.desc.owner) || !Apply::PrepareLink(r, *s)) {
        r.state = STATE_FAILED;
        s->state = STATE_FAILED;
        return false;
    }
    if (s->shape != SHAPE_REWRITE)
        attachLink(r, *s);   // head = this link, its forwarding word = the thunk
    if (!Apply::InstallSite(*s, s_cfg, r.desc.owner)) {
        if (s->shape != SHAPE_REWRITE)
            Chain::Detach(s->chain, s_links, r.index, Apply::Publish);
        r.state = STATE_FAILED;
        return false;
    }
    r.state = STATE_APPLIED;
    return true;
}

void Tick()
{
    ++s_tick;

    if (!s_configured || !s_cfg.dataResolved) {
        blockWanted();
        return;
    }

    const bool retryDue = (s_tick % Apply::kRetryTicks) == 0;
    if (!s_dirty && !Data::Dirty() && !retryDue)
        return;
    s_dirty = false;

    // A data swap needs only the data delta, so it proceeds without a code backend.
    const bool codeReady = s_cfg.textResolved && Apply::EnsureReady(s_cfg);

    // Removals first: a site given up this frame may be what another wants.
    if (codeReady) {
        for (int i = 0; i < s_count; ++i) {
            PatchRecord& r = s_records[i];
            if (r.used && !r.wanted && r.state == STATE_APPLIED)
                removeLink(r);
        }
    }

    findCollisions();

    if (codeReady) {
        // Remember what this tick installed so a failure part way can undo it.
        PatchRecord* batch[kMaxPatches];
        int batched = 0;
        bool failed = false;
        for (int i = 0; i < s_count && !failed; ++i) {
            PatchRecord& r = s_records[i];
            if (!r.used || !r.wanted || r.state == STATE_APPLIED || r.state == STATE_COLLIDED)
                continue;
            if (s_tick < r.nextTryTick)
                continue;
            if (!applyOne(r))
                failed = true;
            else if (r.state == STATE_APPLIED)
                batch[batched++] = &r;
        }
        if (failed && batched) {
            Log::Warn("rolling back %d patch(es) applied this frame", batched);
            for (int i = batched - 1; i >= 0; --i)
                removeLink(*batch[i]);
        }
    } else {
        for (int i = 0; i < s_count; ++i) {
            PatchRecord& r = s_records[i];
            if (r.used && r.wanted && r.state != STATE_APPLIED && r.state != STATE_COLLIDED)
                r.state = STATE_BLOCKED;
        }
    }

    Data::Reconcile(s_cfg, true);
}

const char* StateName(State state)
{
    switch (state) {
    case STATE_DECLARED: return "declared";
    case STATE_BLOCKED:  return "blocked";
    case STATE_REFUSED:  return "refused";
    case STATE_COLLIDED: return "collided";
    case STATE_APPLIED:  return "applied";
    case STATE_FAILED:   return "failed";
    }
    return "?";
}

const char* ShapeName(Shape shape)
{
    switch (shape) {
    case SHAPE_JUMP:    return "jump";
    case SHAPE_CALL:    return "call";
    case SHAPE_REWRITE: return "rewrite";
    }
    return "?";
}

const char* BackendName() { return Apply::BackendName(); }

int         Count()                 { return s_count; }
const char* OwnerAt(int i)          { return (i >= 0 && i < s_count && s_records[i].used) ? s_records[i].desc.owner : ""; }
State       StateAt(int i)          { return (i >= 0 && i < s_count && s_records[i].used) ? s_records[i].state : STATE_DECLARED; }
uint32_t    LinkAddrAt(int i)       { return (i >= 0 && i < s_count && s_records[i].used) ? s_records[i].desc.site.linkAddr : 0; }

uint32_t RuntimeAddrAt(int i)
{
    if (i < 0 || i >= s_count || !s_records[i].used)
        return 0;
    const SiteRecord* s = siteOf(s_records[i]);
    return s ? s->runtimeAddr : 0;
}

int ChainPosition(Handle h)
{
    const PatchRecord* r = at(h);
    if (!r || r->state != STATE_APPLIED)
        return 0;
    const SiteRecord* s = siteOf(*r);
    if (!s)
        return 0;
    return s->shape == SHAPE_REWRITE ? 1 : Chain::Position(s->chain, s_links, r->index);
}

int ChainLength(Handle h)
{
    const PatchRecord* r = at(h);
    if (!r || r->state != STATE_APPLIED)
        return 0;
    const SiteRecord* s = siteOf(*r);
    if (!s)
        return 0;
    return s->shape == SHAPE_REWRITE ? 1 : Chain::Length(s->chain, s_links);
}

int CollisionCount() { return s_collisionCount; }

bool GetCollision(int index, const char** ownerA, const char** ownerB, uint32_t* linkAddr)
{
    if (index < 0 || index >= s_collisionCount)
        return false;
    const Overlap::Collision& c = s_collisions[index];
    if (ownerA)   *ownerA = kindOwner(c.kindA, c.indexA);
    if (ownerB)   *ownerB = kindOwner(c.kindB, c.indexB);
    if (linkAddr) *linkAddr = kindLinkAddr(c.kindB, c.indexB);
    return true;
}

void LogState()
{
    Log::Info("configured=%d text=%08X(%d) data=%08X(%d) backend=%s module=%08X "
              "patches=%d sites=%d swaps=%d collisions=%d",
              (int)s_configured, (unsigned)s_cfg.textDelta, (int)s_cfg.textResolved,
              (unsigned)s_cfg.dataDelta, (int)s_cfg.dataResolved, BackendName(),
              (unsigned)Apply::ModuleTextAddr(), s_count, s_siteCount, Data::Count(),
              s_collisionCount);
    for (int i = 0; i < s_siteCount; ++i) {
        const SiteRecord& s = s_sites[i];
        if (!s.used)
            continue;
        Log::Info("  site %08X -> %08X %s %s thunk=%08X links=%d %s",
                  (unsigned)s.linkAddr, (unsigned)s.runtimeAddr, ShapeName(s.shape),
                  StateName(s.state), (unsigned)s.originalThunk,
                  Chain::Length(s.chain, s_links),
                  s.external ? s.functionName : "");
        int pos = 0;
        for (int cur = s.chain.firstLink; cur != -1; cur = s_links[cur].next)
            Log::Info("    %d. %s", ++pos, s_records[cur].desc.owner);
    }
    for (int i = 0; i < s_count; ++i) {
        const PatchRecord& r = s_records[i];
        if (!r.used)
            continue;
        const SiteRecord* s = siteOf(r);
        Log::Info("  %-24s %08X -> %08X %s%s thunk=%08X", r.desc.owner,
                  (unsigned)r.desc.site.linkAddr, (unsigned)(s ? s->runtimeAddr : 0),
                  StateName(r.state), r.wanted ? " (wanted)" : "",
                  (unsigned)GetOriginalThunk((Handle)(i + 1)));
    }
    for (int i = 0; i < Data::Count(); ++i)
        Log::Info("  %-24s %08X data %s", Data::OwnerAt(i),
                  (unsigned)Data::LinkAddrAt(i), StateName(Data::StateAt(i)));
}

} // namespace WuPatch
