#ifndef ARKCHEMY_SKYLANDERS_FIGURE_H
#define ARKCHEMY_SKYLANDERS_FIGURE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Skylanders figure identification -- the actual "which toy is this"
 * logic, ported from tools/portal_identify.py (the host-side Python tool
 * this was first built and verified against real Portal of Power
 * hardware in) into portable C so it's shared by *both* real figure-
 * reading paths the project plan calls for: Phase 3a (skylanders_portal.c,
 * live USB reads from a real portal) and Phase 3b (this file's own
 * skylanders_figure_load_block, local dumps from SD/storage, no portal
 * needed). Previously this logic only existed in the Python tool --
 * skylanders_portal.c had real USB I/O but zero figure-identification
 * logic of its own.
 *
 * Deliberately has no libnx/<switch.h> dependency -- plain C99 file I/O
 * only (fopen/fread/fseek), identical under devkitA64/libnx's newlib and
 * a normal host libc, so this can be built and unit-tested on a dev
 * machine directly, not just on real Switch hardware.
 *
 * Real block layout (which bytes hold CharacterID/VariantID) confirmed
 * by disassembling the actual game's own tfbSpyroTag class (see
 * tools/portal_identify.py's own detailed comment on this, not
 * reproduced here to avoid drifting out of sync with the source of
 * truth) -- not guessed. CHARACTER_IDS/POP_FIZZ_VARIANTS tables are
 * real, sourced from https://github.com/Texthead1/Skylander-IDs
 * (community-maintained, itself sourced from Portal-To-Unity), ported
 * verbatim from tools/portal_identify.py's own Python dict literal --
 * cross-check the two if either is ever updated independently.
 */

typedef struct SkylandersFigureId {
    int32_t character_id; /* -1 if not present/not decoded */
    int32_t variant_id;   /* -1 if not present/not decoded */
} SkylandersFigureId;

/* Real block layout: CharacterID at bytes 0-1 (little-endian), VariantID
 * at bytes 12-13 (little-endian) of MIFARE Classic block index 1 --
 * exactly the 16 bytes tools/portal_identify.py's main() reads via
 * query_block_with_retry(fd, 1) for its own quick identify path. */
SkylandersFigureId skylanders_figure_decode_block1(const uint8_t block1[16]);

/* Real character name for a CharacterID, or NULL if not in this
 * project's table (a real, but not exhaustive, list -- see the .c file
 * for the full real table and its source). */
const char *skylanders_figure_name(int32_t character_id);

/* Real variant name for a (CharacterID, VariantID) pair, or NULL if
 * either the character has no known variant table, or this specific
 * VariantID isn't in it. Currently only Pop Fizz (CharacterID 108) has
 * a real, captured variant table -- see the .c file. */
const char *skylanders_figure_variant_name(int32_t character_id, int32_t variant_id);

/* Reads one real 16-byte MIFARE Classic block out of a local figure-dump
 * file at `path` -- `block_index` is the same real block numbering used
 * throughout this project (0-63 for a real 1KB MIFARE Classic dump: 16
 * bytes/block * 64 blocks = 1024 bytes total, matching a real full tag
 * dump, not just a single block). Returns false if the file can't be
 * opened or is too short to contain that block. Storage-location-
 * agnostic by design (just a path) -- matches the project plan's
 * Phase 3b requirement to not hardcode a specific storage location; the
 * caller decides which real mounted path (SD card, external drive, ...)
 * to pass in. */
bool skylanders_figure_load_block(const char *path, uint32_t block_index, uint8_t block_out[16]);

/* Convenience: loads block 1 and decodes it in one call -- the common
 * case for a simple "identify this dump" check. Returns false if the
 * dump couldn't be read (figure IDs in the returned struct are
 * meaningless in that case). */
bool skylanders_figure_identify_dump(const char *path, SkylandersFigureId *out);

#define SKYLANDERS_FIGURE_MAX_PATH 512

typedef struct SkylandersDumpEntry {
    char path[SKYLANDERS_FIGURE_MAX_PATH]; /* full path, ready to pass back into the load_block/identify_dump functions above */
    SkylandersFigureId figure;
    const char *name;         /* NULL if character_id isn't in this project's table */
    const char *variant_name; /* NULL if no known variant, or character has no variant table */
} SkylandersDumpEntry;

typedef void (*SkylandersDumpCallback)(const SkylandersDumpEntry *entry, void *user_data);

/* Scans `dir_path` for local figure-dump files -- per the project plan's
 * own Phase 3b requirement ("must not be hardcoded to the SD card
 * specifically... a folder on the SD card, external HDD/USB drive
 * connected to the Switch, or in principle any storage type"),
 * `dir_path` is just a real path the caller chooses (`sdmc:/...` under
 * libnx, an external drive's mount path, or a plain host path when
 * built/tested off-Switch) -- nothing here is SD-card-specific.
 *
 * Recurses exactly one level into subdirectories, matching re_nsyshid's
 * own real convention this project has already noted elsewhere
 * ("SD card figure dumps, browsable subfolders") -- a folder-per-figure
 * or folder-per-box layout works, a deeply nested tree doesn't (a
 * deliberate simplicity choice, not a discovered real constraint).
 *
 * Doesn't rely on a specific file extension (no confirmed real
 * community convention for one) -- instead, every regular file found is
 * a candidate: `skylanders_figure_load_block(path, 1, ...)` is
 * attempted, and only files that genuinely contain a real, readable
 * block 1 (i.e. are at least 32 bytes -- long enough to hold blocks 0
 * and 1) are reported at all, via one `callback` invocation each. An ID
 * that doesn't match this project's `CHARACTER_IDS` table is still
 * reported (with `name = NULL`) rather than silently skipped -- it
 * might be a real figure this table just doesn't cover yet, not
 * necessarily a non-dump file.
 *
 * Returns the number of valid dump files found (0 if none), or -1 if
 * `dir_path` itself couldn't be opened as a directory. */
int skylanders_figure_scan_dir(const char *dir_path, SkylandersDumpCallback callback, void *user_data);

#endif /* ARKCHEMY_SKYLANDERS_FIGURE_H */
