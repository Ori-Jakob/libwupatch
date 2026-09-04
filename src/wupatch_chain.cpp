#include "libwupatch/wupatch_chain.h"

namespace WuPatch {
namespace Chain {

void Init(Site& s, volatile uint32_t* head, uint32_t originalThunk)
{
    s.firstLink = -1;
    s.head = head;
    s.originalThunk = originalThunk;
}

void InitLink(Link& l, volatile uint32_t* nextTarget)
{
    l.next = -1;
    l.priority = 0;
    l.entry = 0;
    l.nextTarget = nextTarget;
    l.attached = false;
}

static int predecessorOf(const Site& s, const Link* links, int idx)
{
    int prev = -1;
    for (int cur = s.firstLink; cur != -1; cur = links[cur].next) {
        if (cur == idx)
            return prev;
        prev = cur;
    }
    return -2;   // not attached
}

void Attach(Site& s, Link* links, int idx, PublishFn publish)
{
    Link& l = links[idx];
    if (l.attached)
        return;

    int prev = -1;
    int cur = s.firstLink;
    while (cur != -1 && links[cur].priority >= l.priority) {
        prev = cur;
        cur = links[cur].next;
    }

    // The new link's forwarding word first, so nothing points at an incomplete link.
    l.next = cur;
    publish(l.nextTarget, cur != -1 ? links[cur].entry : s.originalThunk);

    if (prev == -1) {
        s.firstLink = idx;
        publish(s.head, l.entry);
    } else {
        links[prev].next = idx;
        publish(links[prev].nextTarget, l.entry);
    }
    l.attached = true;
}

void Detach(Site& s, Link* links, int idx, PublishFn publish)
{
    Link& l = links[idx];
    const int prev = predecessorOf(s, links, idx);
    if (prev == -2)
        return;

    const uint32_t successor = l.next != -1 ? links[l.next].entry : s.originalThunk;
    if (prev == -1) {
        s.firstLink = l.next;
        // An emptied head keeps pointing at the departing link; the site is restored next.
        if (l.next != -1)
            publish(s.head, successor);
    } else {
        links[prev].next = l.next;
        publish(links[prev].nextTarget, successor);
    }
    // l.nextTarget is deliberately left: the link forwards until the process ends.
    l.next = -1;
    l.attached = false;
}

void Clear(Site& s, Link* links)
{
    int cur = s.firstLink;
    while (cur != -1) {
        const int next = links[cur].next;
        links[cur].next = -1;
        links[cur].attached = false;
        cur = next;
    }
    s.firstLink = -1;
}

int Length(const Site& s, const Link* links)
{
    int n = 0;
    for (int cur = s.firstLink; cur != -1; cur = links[cur].next)
        ++n;
    return n;
}

int Position(const Site& s, const Link* links, int idx)
{
    int n = 0;
    for (int cur = s.firstLink; cur != -1; cur = links[cur].next) {
        ++n;
        if (cur == idx)
            return n;
    }
    return 0;
}

} // namespace Chain
} // namespace WuPatch
