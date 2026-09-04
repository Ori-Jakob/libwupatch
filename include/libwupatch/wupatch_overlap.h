#pragma once

#include <stdint.h>

namespace WuPatch {
namespace Overlap {

struct Extent {
    uint32_t start;
    uint32_t end;       // exclusive
    uint8_t  segment;   // Segment
    uint8_t  kind;      // caller's table id; opaque here
    uint8_t  index;     // caller's row in that table; opaque here
    bool     applied;   // already installed, so it wins a collision
};

struct Collision {
    uint8_t  kindA, indexA;   // the extent that was there first
    uint8_t  kindB, indexB;   // the one that collides with it
    uint32_t start;
};

static const int kMaxCollisions = 8;

// Sorts `extents` by (segment, start) and reports overlaps, adjacent or not.
int Find(Extent* extents, int count, Collision* out, int outCap);

} // namespace Overlap
} // namespace WuPatch
