#include "libwupatch/wupatch_overlap.h"

namespace WuPatch {
namespace Overlap {

static bool before(const Extent& a, const Extent& b)
{
    if (a.segment != b.segment)
        return a.segment < b.segment;
    if (a.start != b.start)
        return a.start < b.start;
    // On a tie, applied sorts first so it is reported as the one already there.
    return a.applied && !b.applied;
}

int Find(Extent* extents, int count, Collision* out, int outCap)
{
    // Insertion sort: a few dozen extents, and only on a dirty tick.
    for (int i = 1; i < count; ++i) {
        const Extent value = extents[i];
        int j = i - 1;
        while (j >= 0 && before(value, extents[j])) {
            extents[j + 1] = extents[j];
            --j;
        }
        extents[j + 1] = value;
    }

    int found = 0;
    // Track the furthest reach, not the predecessor: overlaps need not be adjacent.
    int reach = -1;
    for (int i = 0; i < count; ++i) {
        const Extent& e = extents[i];
        if (reach >= 0 && extents[reach].segment == e.segment &&
            e.start < extents[reach].end) {
            if (found < outCap) {
                Collision& c = out[found];
                c.kindA  = extents[reach].kind;
                c.indexA = extents[reach].index;
                c.kindB  = e.kind;
                c.indexB = e.index;
                c.start  = e.start;
            }
            ++found;
        }
        if (reach < 0 || extents[reach].segment != e.segment ||
            e.end > extents[reach].end)
            reach = i;
    }
    return found < outCap ? found : outCap;
}

} // namespace Overlap
} // namespace WuPatch
