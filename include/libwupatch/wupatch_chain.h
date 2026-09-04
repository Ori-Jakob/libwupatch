#pragma once

#include <stdint.h>

namespace WuPatch {
namespace Chain {

// One store per edit, so a core mid-chain sees the old successor or the new one, both valid.
typedef void (*PublishFn)(volatile uint32_t* word, uint32_t value);

struct Link {
    int      next;                   // index of the next link, -1 for the tail
    int32_t  priority;               // higher runs earlier
    uint32_t entry;                  // what a predecessor jumps to: the hook, or the CALL shim
    volatile uint32_t* nextTarget;   // this link's forwarding word
    bool     attached;
};

struct Site {
    int      firstLink;              // -1 when empty
    volatile uint32_t* head;         // the dispatcher's word
    uint32_t originalThunk;          // what the tail forwards to
};

void Init(Site& s, volatile uint32_t* head, uint32_t originalThunk);
void InitLink(Link& l, volatile uint32_t* nextTarget);

// Insert after every link with priority >= the new one's; publishes the new link first.
void Attach(Site& s, Link* links, int idx, PublishFn publish);

// The detached link's forwarding word stays valid, and an emptied head is left alone.
void Detach(Site& s, Link* links, int idx, PublishFn publish);

// Forget the list without touching any word, for a site that has just been restored.
void Clear(Site& s, Link* links);

int Length(const Site& s, const Link* links);
int Position(const Site& s, const Link* links, int idx);   // 1-based; 0 if not attached

} // namespace Chain
} // namespace WuPatch
