#ifndef ARK_JPROBE_H
#define ARK_JPROBE_H

/* Probe state that belongs to jouster rather than to conquertron's runtime:
 * tools/probe-hooks.py includes this only in the generated chunks that call
 * into it, and main.c includes it to print what it collected.
 *
 * BLKWRITE, added 2026-09-28. The archive's block pool holds six blocks, and
 * by the end of a 60-second run all six are in use and every further
 * allocate fails (alloc=71 gotBlock=0), so no read is issued after the 65th.
 * RELFIX handed 55 held blocks back as cached and changed nothing, so the
 * question is what writes a block's _state (+0x14), where, and with what.
 * Every `stw rS, 0x14(rB)` in the archive task and block code that is not a
 * stack spill is hooked; per site this keeps the calls, a histogram of the
 * values written, and the last base and value. Not every site is a block --
 * tasks have a +0x14 too -- which is what lastbase is for: match it against
 * BLKPOOL's six blocks. */

#include <stdint.h>

#define ARK_BLKW_SITES 24

#ifdef __GNUC__
__attribute__((weak))
#endif
volatile uint32_t g_ark_blkw[ARK_BLKW_SITES][8]; /* site, calls, =0, =1, =2, other, lastbase, lastval */
#ifdef __GNUC__
__attribute__((weak))
#endif
volatile uint32_t g_ark_blkw_n = 0, g_ark_blkw_over = 0;

/* The igArchiveBlockManager, as getNumAvailableBlocks last saw it (r3), so
 * BLKPOOL can walk the pool at exit. */
#ifdef __GNUC__
__attribute__((weak))
#endif
volatile uint32_t g_ark_blkmgr = 0;

static inline void ark_blkw(uint32_t site, uint32_t base, uint32_t val)
{
    uint32_t i;
    for (i = 0; i < g_ark_blkw_n && i < ARK_BLKW_SITES; i++)
        if (g_ark_blkw[i][0] == site) break;
    if (i >= ARK_BLKW_SITES) { g_ark_blkw_over++; return; }
    if (i == g_ark_blkw_n) { g_ark_blkw[i][0] = site; g_ark_blkw_n++; }
    g_ark_blkw[i][1]++;
    g_ark_blkw[i][val <= 2u ? 2u + val : 5u]++;
    g_ark_blkw[i][6] = base;
    g_ark_blkw[i][7] = val;
}

#endif
