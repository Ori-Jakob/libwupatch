#include <stdio.h>

#include "libwupatch/wupatch_chain.h"
#include "libwupatch/wupatch_overlap.h"
#include "libwupatch/wupatch_ppc.h"
#include "libwupatch/wupatch_reloc.h"
#include "libwupatch/wupatch_shim.h"
#include "libwupatch/wupatch_types.h"

using namespace WuPatch;

static int g_fail = 0;

static void chk(const char* what, bool ok)
{
    if (!ok) g_fail++;
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
}

static void chkWord(const char* what, uint32_t got, uint32_t want)
{
    const bool ok = got == want;
    if (!ok) g_fail++;
    printf("  %-58s %08X %s\n", what, (unsigned)got, ok ? "ok" : "FAIL");
    if (!ok) printf("  %-58s expected %08X\n", "", (unsigned)want);
}

static void testEncoders()
{
    printf("encoders\n");
    // ori is unsigned, so lis carries 0x80D7 plain; the +0x8000 form lands 0x10000 past.
    chkWord("lis r11, hi(0x80D7C03C)", Ppc::Lis(11, 0x80D7C03Cu), 0x3D6080D7u);
    chkWord("ori r11, r11, lo(0x80D7C03C)", Ppc::Ori(11, 0x80D7C03Cu), 0x616BC03Cu);
    chkWord("mtctr r11", Ppc::MtCtr(11), 0x7D6903A6u);
    chkWord("mtlr r11", Ppc::MtLr(11), 0x7D6803A6u);
    chkWord("bctr", Ppc::Bctr(), 0x4E800420u);
    chkWord("blr", Ppc::Blr(), 0x4E800020u);
    chkWord("nop", Ppc::Nop(), 0x60000000u);
    chkWord("kNop is nop", kNop, Ppc::Nop());
    chkWord("lfs f31, 0x5638(r12)", Ppc::Lfs(31, 12, 0x5638), 0xC3EC5638u);
    chkWord("lwz r11, 0(r11)", Ppc::Lwz(11, 11, 0), 0x816B0000u);
    chkWord("lwz r3, -4(r1)", Ppc::Lwz(3, 1, -4), 0x8061FFFCu);

    uint32_t w = 0;
    chk("b +0x10 encodes", Ppc::EncodeBranch(0x02000000u, 0x02000010u, false, &w));
    chkWord("  b +0x10", w, 0x48000010u);
    chk("bl +0x10 encodes", Ppc::EncodeBranch(0x02000000u, 0x02000010u, true, &w));
    chkWord("  bl +0x10", w, 0x48000011u);
    chk("b -0x10 encodes", Ppc::EncodeBranch(0x02000010u, 0x02000000u, false, &w));
    chkWord("  b -0x10", w, 0x4BFFFFF0u);
    chk("b +0x01FFFFFC is the last reachable", Ppc::EncodeBranch(0u, 0x01FFFFFCu, false, &w));
    chk("b +0x02000000 is out of range", !Ppc::EncodeBranch(0u, 0x02000000u, false, &w));
    chk("b -0x02000000 is the first reachable", Ppc::EncodeBranch(0x02000000u, 0u, false, &w));
    chkWord("  b -0x02000000", w, 0x4A000000u);
    chk("plugin at 0x80D7C03C is out of range from 0x0203F5D8",
        !Ppc::EncodeBranch(0x0203F5D8u, 0x80D7C03Cu, false, &w));
    chk("unaligned target refused", !Ppc::EncodeBranch(0x02000000u, 0x02000012u, false, &w));
    chk("ba 0x00123458 encodes", Ppc::EncodeAbsoluteBranch(0x00123458u, false, &w));
    chkWord("  ba 0x00123458", w, 0x4812345Au);
    chk("ba 0x02000000 is out of range", !Ppc::EncodeAbsoluteBranch(0x02000000u, false, &w));
    chk("ba unaligned refused", !Ppc::EncodeAbsoluteBranch(0x00123456u, false, &w));
}

static void testClassify()
{
    printf("classify\n");
    chk("b +0x124 is PC-relative",  Ppc::Classify(0x48000124u) == Ppc::INSTR_PC_RELATIVE);
    chk("beq +0x14 is PC-relative", Ppc::Classify(0x41820014u) == Ppc::INSTR_PC_RELATIVE);
    chk("ba is PIC",                Ppc::Classify(0x4812345Au) == Ppc::INSTR_PIC);
    chk("crxor is PIC",             Ppc::Classify(0x4CC63182u) == Ppc::INSTR_PIC);
    chk("or r6,r5,r5 is PIC",       Ppc::Classify(0x7CA62B78u) == Ppc::INSTR_PIC);
    chk("lfs is PIC",               Ppc::Classify(0xC3EC5638u) == Ppc::INSTR_PIC);
    chk("addi is PIC",              Ppc::Classify(0x38A52FA4u) == Ppc::INSTR_PIC);
    chk("stfs is PIC",              Ppc::Classify(0xD17C0340u) == Ppc::INSTR_PIC);
    chk("stwu r1,-0x20(r1) is PIC", Ppc::Classify(0x9421FFE0u) == Ppc::INSTR_PIC);
    chk("mflr r0 is PIC",           Ppc::Classify(0x7C0802A6u) == Ppc::INSTR_PIC);
    chk("mtlr r0 is PIC",           Ppc::Classify(0x7C0803A6u) == Ppc::INSTR_PIC);
    chk("blr is PIC",               Ppc::Classify(0x4E800020u) == Ppc::INSTR_PIC);
    // The stub owns CTR between its mtctr and its bctr.
    chk("mtctr r11 is unknown",     Ppc::Classify(0x7D6903A6u) == Ppc::INSTR_UNKNOWN);
    chk("mtctr r0 is unknown",      Ppc::Classify(0x7C0903A6u) == Ppc::INSTR_UNKNOWN);
    chk("mfctr r0 is unknown",      Ppc::Classify(0x7C0902A6u) == Ppc::INSTR_UNKNOWN);
    chk("bctr is unknown",          Ppc::Classify(0x4E800420u) == Ppc::INSTR_UNKNOWN);
    chk("bctrl is unknown",         Ppc::Classify(0x4E800421u) == Ppc::INSTR_UNKNOWN);
    chk("zero word is unknown",     Ppc::Classify(0x00000000u) == Ppc::INSTR_UNKNOWN);
    chk("sc is unknown",            Ppc::Classify(0x44000002u) == Ppc::INSTR_UNKNOWN);
}

static void testReloc()
{
    printf("relocation\n");
    const uint32_t tphdDelta = 0x00502800u;
    const uint32_t wwhdDelta = 0x00502200u;

    // tphd's documented false refusal: a D-form load with the low half moved.
    chkWord("lfs C02A7090 + 0x2800 low", Reloc::RelocateLowHalf(0xC02A7090u, tphdDelta), 0xC02A9890u);
    chk("  MatchesNative accepts it", Reloc::MatchesNative(0xC02A9890u, 0xC02A7090u, tphdDelta));

    // libwwhd's region probe, verbatim: lis with the high half moved by 0x50.
    chk("lis 3CA01014 -> 3CA01064 (hi)",   Reloc::MatchesNative(0x3CA01064u, 0x3CA01014u, wwhdDelta));
    chk("lis 3CA01014 -> 3CA01065 (hi+1)", Reloc::MatchesNative(0x3CA01065u, 0x3CA01014u, wwhdDelta));
    chk("lis 3CA01014 -> 3CA01099 refused", !Reloc::MatchesNative(0x3CA01099u, 0x3CA01014u, wwhdDelta));

    // The gap tphd would refuse: addi carries a low half too.
    chkWord("addi 38A50000 + 0x2200 low", Reloc::RelocateLowHalf(0x38A50000u, wwhdDelta), 0x38A52200u);
    chk("  MatchesNative accepts it", Reloc::MatchesNative(0x38A52200u, 0x38A50000u, wwhdDelta));
    chkWord("ori 616B0000 + 0x2200 low", Reloc::RelocateLowHalf(0x616B0000u, wwhdDelta), 0x616B2200u);

    // Wraparound in the low half is modular, exactly as the loader does it.
    chkWord("low half wraps", Reloc::RelocateLowHalf(0xC02AF000u, 0x00002000u), 0xC02A1000u);

    chk("delta 0 is identity",     Reloc::MatchesNative(0xC02A7090u, 0xC02A7090u, 0u));
    chk("delta 0 rejects a change", !Reloc::MatchesNative(0xC02A7094u, 0xC02A7090u, 0u));
    chk("a non-relocatable form is not 'relocated' into a match",
        !Reloc::MatchesNative(0x7CA62B79u, 0x7CA62B78u, wwhdDelta));
    chkWord("or r6,r5,r5 is left alone", Reloc::RelocateLowHalf(0x7CA62B78u, wwhdDelta), 0x7CA62B78u);
    chk("a branch word is not relocated", !Reloc::MatchesNative(0x48002324u, 0x48000124u, wwhdDelta));
}

static void testShims()
{
    printf("shims\n");
    uint32_t out[Shim::kSlotWords] = {};
    const uint32_t ret  = 0x0E73AA28u;
    const uint32_t hook = 0x80D7C03Cu;   // low half has bit 15 set

    chk("call shim is 7 words", Shim::BuildCall(out, ret, hook) == Shim::kCallWords);
    chkWord("  lis r11, hi(ret)",   out[0], 0x3D600E73u);
    chkWord("  ori r11, lo(ret)",   out[1], 0x616BAA28u);
    chkWord("  mtlr r11",           out[2], 0x7D6803A6u);
    chkWord("  lis r11, hi(hook)",  out[3], 0x3D6080D7u);
    chkWord("  ori r11, lo(hook)",  out[4], 0x616BC03Cu);
    chkWord("  mtctr r11",          out[5], 0x7D6903A6u);
    chkWord("  bctr",               out[6], 0x4E800420u);

    const uint32_t replacement = 0xC3EC5638u;
    chk("rewrite shim is 5 words", Shim::BuildRewrite(out, ret, replacement) == Shim::kRewriteWords);
    chkWord("  lis r11, hi(ret)",   out[0], 0x3D600E73u);
    chkWord("  ori r11, lo(ret)",   out[1], 0x616BAA28u);
    chkWord("  mtctr r11",          out[2], 0x7D6903A6u);
    chkWord("  <replacement>",      out[3], replacement);
    chkWord("  bctr",               out[4], 0x4E800420u);

    // The pointer's low half has bit 15 set, like the hook above.
    const uint32_t ptr = 0x80D7C03Cu;
    chk("indirect shim is 5 words", Shim::BuildIndirect(out, ptr) == Shim::kIndirectWords);
    chkWord("  lis r11, hi(ptr)",   out[0], 0x3D6080D7u);
    chkWord("  ori r11, lo(ptr)",   out[1], 0x616BC03Cu);
    chkWord("  lwz r11, 0(r11)",    out[2], 0x816B0000u);
    chkWord("  mtctr r11",          out[3], 0x7D6903A6u);
    chkWord("  bctr",               out[4], 0x4E800420u);

    chk("all fit a slot", Shim::kCallWords <= Shim::kSlotWords &&
                          Shim::kRewriteWords <= Shim::kSlotWords &&
                          Shim::kIndirectWords <= Shim::kSlotWords);
    chk("site slot: entry and thunk on different cache lines",
        Shim::kSiteEntryOffset + Shim::kIndirectWords <= 8 &&
        Shim::kSiteThunkOffset >= 8 &&
        Shim::kSiteThunkOffset + Shim::kRewriteWords <= Shim::kSlotWords);
    chk("link slot: next and call on different cache lines",
        Shim::kLinkNextOffset + Shim::kIndirectWords <= 8 &&
        Shim::kLinkCallOffset >= 8 &&
        Shim::kLinkCallOffset + Shim::kCallWords <= Shim::kSlotWords);
}

static Overlap::Extent ext(uint32_t start, uint32_t end, uint8_t seg, uint8_t index, bool applied)
{
    Overlap::Extent e;
    e.start = start; e.end = end; e.segment = seg; e.kind = 0; e.index = index; e.applied = applied;
    return e;
}

static void testOverlap()
{
    printf("overlap\n");
    Overlap::Collision c[Overlap::kMaxCollisions];

    // Same site twice collides; the adjacent word does not.
    {
        Overlap::Extent e[3] = {
            ext(0x100, 0x104, SEG_TEXT, 0, false),
            ext(0x100, 0x104, SEG_TEXT, 1, false),
            ext(0x104, 0x108, SEG_TEXT, 2, false),
        };
        const int n = Overlap::Find(e, 3, c, Overlap::kMaxCollisions);
        chk("same site -> one collision", n == 1);
        chk("  reported at the site", n == 1 && c[0].start == 0x100);
        chk("  adjacent word is free", n == 1 && c[0].indexA != 2 && c[0].indexB != 2);
    }
    // A table and a word inside it.
    {
        Overlap::Extent e[2] = {
            ext(0x1050, 0x1054, SEG_DATA, 1, false),
            ext(0x1000, 0x10A0, SEG_DATA, 0, false),
        };
        const int n = Overlap::Find(e, 2, c, Overlap::kMaxCollisions);
        chk("word inside a table collides", n == 1);
        chk("  the table is A, the word is B", n == 1 && c[0].indexA == 0 && c[0].indexB == 1);
    }
    // Two short extents inside a long one; a neighbour scan misses the second.
    {
        Overlap::Extent e[3] = {
            ext(0x180, 0x184, SEG_TEXT, 2, false),
            ext(0x000, 0x200, SEG_TEXT, 0, true),
            ext(0x100, 0x104, SEG_TEXT, 1, false),
        };
        const int n = Overlap::Find(e, 3, c, Overlap::kMaxCollisions);
        chk("both words inside the long extent collide", n == 2);
        chk("  both against the long one", n == 2 && c[0].indexA == 0 && c[1].indexA == 0);
    }
    // The same address in different segments is not a collision.
    {
        Overlap::Extent e[2] = {
            ext(0x100, 0x104, SEG_TEXT, 0, false),
            ext(0x100, 0x104, SEG_DATA, 1, false),
        };
        chk("text and data do not collide", Overlap::Find(e, 2, c, Overlap::kMaxCollisions) == 0);
    }
    // Applied sorts first on a tie, so it is the one reported as already there.
    {
        Overlap::Extent e[2] = {
            ext(0x100, 0x104, SEG_TEXT, 5, false),
            ext(0x100, 0x104, SEG_TEXT, 6, true),
        };
        const int n = Overlap::Find(e, 2, c, Overlap::kMaxCollisions);
        chk("applied extent wins the tie", n == 1 && c[0].indexA == 6 && c[0].indexB == 5);
    }
    // Output cap is honoured.
    {
        Overlap::Extent e[4] = {
            ext(0x100, 0x104, SEG_TEXT, 0, false), ext(0x100, 0x104, SEG_TEXT, 1, false),
            ext(0x100, 0x104, SEG_TEXT, 2, false), ext(0x100, 0x104, SEG_TEXT, 3, false),
        };
        chk("collision cap honoured", Overlap::Find(e, 4, c, 2) == 2);
    }
}

// `publish` records store order, because the order is the safety argument.
static uint32_t g_storeLog[32];
static int      g_stores = 0;

static void publish(volatile uint32_t* word, uint32_t value)
{
    *word = value;
    if (g_stores < 32)
        g_storeLog[g_stores] = value;
    ++g_stores;
}

static void testChain()
{
    printf("chain\n");
    volatile uint32_t head = 0;
    volatile uint32_t fwd[4] = { 0, 0, 0, 0 };
    const uint32_t thunk = 0x80D70100u;
    const uint32_t entry[4] = { 0x80D71000u, 0x80D72000u, 0x80D73000u, 0x80D74000u };

    Chain::Site s;
    Chain::Link l[4];
    Chain::Init(s, &head, thunk);
    for (int i = 0; i < 4; ++i) {
        Chain::InitLink(l[i], &fwd[i]);
        l[i].entry = entry[i];
    }
    chk("empty chain has length 0", Chain::Length(s, l) == 0);

    // First link: its forwarding word gets the thunk before the head names it.
    g_stores = 0;
    Chain::Attach(s, l, 0, publish);
    chk("first attach: head -> link0", head == entry[0]);
    chk("  link0 -> thunk", fwd[0] == thunk);
    chk("  forwarding word stored before the head", g_stores == 2 && g_storeLog[0] == thunk && g_storeLog[1] == entry[0]);
    chk("  length 1, position 1", Chain::Length(s, l) == 1 && Chain::Position(s, l, 0) == 1);

    // Equal priority appends: link1 runs after link0.
    Chain::Attach(s, l, 1, publish);
    chk("equal priority appends", head == entry[0] && fwd[0] == entry[1] && fwd[1] == thunk);
    chk("  positions 1 and 2", Chain::Position(s, l, 0) == 1 && Chain::Position(s, l, 1) == 2);

    // Higher priority goes first: link2 becomes the head.
    l[2].priority = 10;
    g_stores = 0;
    Chain::Attach(s, l, 2, publish);
    chk("higher priority becomes head", head == entry[2] && fwd[2] == entry[0]);
    chk("  rest untouched", fwd[0] == entry[1] && fwd[1] == thunk);
    chk("  its forwarding word stored before the head", g_stores == 2 && g_storeLog[0] == entry[0] && g_storeLog[1] == entry[2]);
    chk("  length 3", Chain::Length(s, l) == 3);

    // Middle priority lands between: link3 (5) after link2 (10), before link0 (0).
    l[3].priority = 5;
    Chain::Attach(s, l, 3, publish);
    chk("middle priority inserts in place", head == entry[2] && fwd[2] == entry[3] && fwd[3] == entry[0] && fwd[0] == entry[1] && fwd[1] == thunk);
    chk("  order 2,3,0,1", Chain::Position(s, l, 2) == 1 && Chain::Position(s, l, 3) == 2 &&
                            Chain::Position(s, l, 0) == 3 && Chain::Position(s, l, 1) == 4);

    // Attaching twice is a no-op.
    g_stores = 0;
    Chain::Attach(s, l, 3, publish);
    chk("re-attach is a no-op", g_stores == 0 && Chain::Length(s, l) == 4);

    // Detach a middle link: the predecessor skips it, the link still forwards.
    g_stores = 0;
    Chain::Detach(s, l, 3, publish);
    chk("detach middle: predecessor skips it", fwd[2] == entry[0]);
    chk("  detached link still forwards", fwd[3] == entry[0]);
    chk("  one store", g_stores == 1);
    chk("  not in the chain", Chain::Position(s, l, 3) == 0 && Chain::Length(s, l) == 3);

    // Detach the head: the head word moves on.
    Chain::Detach(s, l, 2, publish);
    chk("detach head: head -> next", head == entry[0]);
    chk("  old head still forwards", fwd[2] == entry[0]);

    // Detach the tail: the predecessor now forwards to the thunk.
    Chain::Detach(s, l, 1, publish);
    chk("detach tail: predecessor -> thunk", fwd[0] == thunk);

    // Detach the last one: the head is left alone.
    g_stores = 0;
    Chain::Detach(s, l, 0, publish);
    chk("detach last: head left as it was", head == entry[0] && g_stores == 0);
    chk("  chain empty", Chain::Length(s, l) == 0 && s.firstLink == -1);
    chk("  last link still forwards to the thunk", fwd[0] == thunk);

    // Detaching something not attached is a no-op.
    g_stores = 0;
    Chain::Detach(s, l, 1, publish);
    chk("detach of an unattached link is a no-op", g_stores == 0);

    // Clear forgets without storing.
    Chain::Attach(s, l, 0, publish);
    Chain::Attach(s, l, 1, publish);
    g_stores = 0;
    Chain::Clear(s, l);
    chk("clear: no stores, chain empty", g_stores == 0 && Chain::Length(s, l) == 0);
    chk("  links detached", !l[0].attached && !l[1].attached);
    chk("  words untouched", head == entry[0] && fwd[0] == entry[1] && fwd[1] == thunk);
}

int main()
{
    testEncoders();
    testClassify();
    testReloc();
    testShims();
    testOverlap();
    testChain();
    printf("\n%s\n", g_fail ? "FAILURES" : "all libwupatch host checks passed");
    return g_fail ? 1 : 0;
}
