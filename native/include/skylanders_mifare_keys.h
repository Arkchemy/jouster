#ifndef BRAMBLE_SKYLANDERS_MIFARE_KEYS_H
#define BRAMBLE_SKYLANDERS_MIFARE_KEYS_H

#include <stdint.h>

/*
 * Real Skylanders MIFARE Classic Key A derivation -- the missing piece
 * for reading a figure directly off the Switch's own NFC hardware
 * (libnx's nfc.h documents real Mifare Classic support, and its
 * NfcTagType_Mifare enum entry is literally commented "Skylanders" --
 * see switch/native/source/skylanders_nfc.c). A real Portal of Power
 * (skylanders_portal.c) never needs this: the portal's own reader chip
 * handles MIFARE authentication internally and returns already-
 * decrypted block data over USB. Reading via Joy-Con NFC means this
 * project has to do that authentication step itself.
 *
 * Skylanders figures do NOT use the standard MIFARE Classic default key
 * (FFFFFFFFFFFF) -- confirmed via web research this session, not
 * assumed. Instead: sector 0 uses one real fixed key, and sectors 1-15
 * each use a key *derived from the tag's own 4-byte UID plus the sector
 * number*, via a real algorithm published by the community
 * (skylandersNFC/SkyKeys-Generator, a webapp specifically built to
 * generate these keys from a figure's UID) -- not guessed, and not
 * something this project derived independently.
 *
 * Ported from that project's real script.js (computeCRC48 + getKeyA),
 * fetched directly, not paraphrased. Verified by running the actual
 * original JavaScript (via node) side-by-side with this C port for 5
 * different test UIDs across all 16 sectors (80 keys total) and
 * confirming an exact byte-for-byte match -- those same 80
 * real-JS-verified vectors are kept as a permanent regression test in
 * tools/test_skylanders_mifare_keys.c (not part of the switch/native/
 * build -- it has its own main(), kept alongside tools/gen_harness*.c's
 * same host-testable-only convention), not just a one-time check thrown
 * away after this session.
 *
 * NOT independently confirmed against a real physical figure's actual
 * NFC UID and a real successful MIFARE authentication -- that needs
 * real Joy-Con hardware and a real figure, which is Phase 3c's own
 * "needs real hardware testing, not just a plan" requirement (per the
 * project plan's own Phase 3c section) on top of this. What *is*
 * verified here is that this code computes the exact same output the
 * community's own real, working tool does for a given UID -- a
 * necessary, not sufficient, condition for this being useful on real
 * hardware.
 */

/* Computes the real per-sector Skylanders MIFARE Classic Key A for a
 * figure with the given 4-byte UID (as read from the tag's own block 0,
 * unencrypted/unauthenticated per every real MIFARE Classic tag's
 * standard manufacturer block). `sector` must be 0-15 (a real MIFARE
 * Classic 1K tag's real sector count) -- out-of-range values are
 * clamped to sector 0's key rather than reading past `key_out`, a
 * defensive choice since real callers should never hit this path if
 * they're iterating 0-15 correctly. */
void skylanders_mifare_key_a(int sector, const uint8_t uid[4], uint8_t key_out[6]);

#endif /* BRAMBLE_SKYLANDERS_MIFARE_KEYS_H */
