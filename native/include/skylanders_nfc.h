#ifndef ARKCHEMY_SKYLANDERS_NFC_H
#define ARKCHEMY_SKYLANDERS_NFC_H

#include <stdbool.h>

#include "skylanders_figure.h"

/*
 * Phase 3c: reading a Skylanders figure directly off the Switch's own
 * NFC hardware (a Joy-Con or Pro Controller), no Portal of Power
 * needed. This project's plan explicitly flagged this as needing real
 * hardware testing before committing to it as a supported path
 * ("Figure out whether original-generation Skylanders NFC tags
 * (MIFARE Classic-based) are physically/protocol-compatible with the
 * Joy-Con's amiibo reader (NTAG215-oriented) before committing to this
 * as a supported path -- this needs real hardware testing, not just a
 * plan"). This file is exactly that: real API calls against libnx's
 * actual current nfc.h (fetched directly, not paraphrased -- see
 * skylanders_mifare_keys.h for the real key-derivation piece this
 * depends on), compiled clean against the real devkitA64 toolchain --
 * but NOT run against a real Joy-Con and a real Skylanders figure,
 * which this dev environment has neither of. Same
 * "***UNVERIFIED***" standard as skylanders_portal.c, but one real
 * step ahead of it: that file was only ever compiled against a
 * hand-written stub header (no devkitA64 toolchain reachable in that
 * session); this one is compiled against libnx's real headers.
 *
 * The strongest real evidence this path is worth attempting at all:
 * libnx's own nfc.h literally comments its Mifare Classic tag-type
 * enum entry `NfcTagType_Mifare` with "Skylanders" -- a real,
 * authoritative signal (from the library itself, not a guess) that
 * the Switch's NFC hardware has documented Mifare Classic support,
 * not just amiibo/NTAG215.
 *
 * Real flow, using libnx's dedicated Mifare-only sub-service
 * (nfcMf*, simpler than juggling the generic amiibo-oriented nfc/nfp
 * services for a non-amiibo tag type):
 *   1. nfcMfInitialize() / nfcMfListDevices() -- find a real NFC-
 *      capable controller (Joy-Con/Pro Controller).
 *   2. nfcMfStartDetection() then poll nfcMfGetDeviceState() for
 *      NfcMifareDeviceState_TagFound -- a real tag is physically near
 *      the controller's NFC point.
 *   3. nfcMfGetTagInfo() -- the tag's real 4-byte UID (this specific
 *      Skylanders figure's own chip serial number).
 *   4. skylanders_mifare_key_a(0, uid, key) -- CharacterID/VariantID
 *      live in block 1, which is in MIFARE sector 0 (blocks 0-3 per
 *      sector on a real 1K tag), and sector 0 always uses the same
 *      fixed key regardless of UID -- no per-figure key derivation
 *      needed for *this specific* read, only for sectors 1-15's own
 *      figure-specific save/inventory data, which this file doesn't
 *      attempt to read.
 *   5. nfcMfReadMifare() with that key to get block 1's real 16 bytes,
 *      then skylanders_figure_decode_block1() (already real, already
 *      unit-tested against tools/portal_identify.py's own confirmed
 *      block layout) to get the actual CharacterID/VariantID.
 *
 * One real, honestly-flagged uncertainty in step 5: libnx's
 * `NfcMifareReadBlockParameter`/`NfcMifareReadBlockData` structs name
 * their addressing field `sector_number`, with no header comment
 * clarifying whether it means a real MIFARE *sector* (0-15, 4 blocks
 * each) or an absolute *block* index (0-63) -- this implementation
 * assumes the latter (an absolute block index), since that's the only
 * reading that makes a per-call, per-block round-trip
 * (`NfcMifareReadBlockData` echoes the same field back with exactly
 * one 16-byte block of data) make sense. Not confirmed against real
 * hardware behavior.
 */

/* Initializes libnx's Mifare-only NFC sub-service. Returns false if the
 * service can't be reached -- a real, non-fatal outcome on hardware
 * that has no NFC-capable controller connected/awake, same reasoning
 * as skylanders_portal.h's portal_init(). */
bool skylanders_nfc_init(void);

/* Releases the NFC service. Safe to call even if init never succeeded. */
void skylanders_nfc_exit(void);

/* Blocks briefly (a few real seconds, internally polling real device/
 * tag state) waiting for a real figure to be held near an NFC-capable
 * controller, then reads and decodes its CharacterID/VariantID.
 * Returns false on any real failure: no NFC-capable controller found,
 * no tag detected in time, or a real MIFARE authentication/read
 * failure (e.g. if the real key-derivation algorithm this depends on
 * turns out not to match real hardware once tested). */
bool skylanders_nfc_read_figure(SkylandersFigureId *out);

#endif /* ARKCHEMY_SKYLANDERS_NFC_H */
