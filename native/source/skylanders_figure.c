#include "skylanders_figure.h"

#include <stdio.h>
#include <string.h>

/*
 * CHARACTER_IDS, ported verbatim (same IDs, same names) from
 * tools/portal_identify.py's own CHARACTER_IDS dict -- real, sourced
 * from https://github.com/Texthead1/Skylander-IDs (community-
 * maintained, itself sourced from Portal-To-Unity), not guessed. Kept
 * as a flat, unsorted array of {id, name} pairs (not a switch/hash
 * table) since insertion order matching the Python source makes
 * keeping the two in sync by eye easy -- this table is small enough
 * that a linear scan is not a real performance concern.
 */
typedef struct {
    int32_t id;
    const char *name;
} CharacterIdEntry;

static const CharacterIdEntry CHARACTER_IDS[] = {
    {0, "Whirlwind"}, {1, "Sonic Boom"}, {2, "Warnado"}, {3, "Lightning Rod"},
    {4, "Bash"}, {5, "Terrafin"}, {6, "Dino-Rang"}, {7, "Prism Break"},
    {8, "Sunburn"}, {9, "Eruptor"}, {10, "Ignitor"}, {11, "Flameslinger"},
    {12, "Zap"}, {13, "Wham-Shell"}, {14, "Gill Grunt"}, {15, "Slam Bam"},
    {16, "Spyro"}, {17, "Voodood"}, {18, "Double Trouble"}, {19, "Trigger Happy"},
    {20, "Drobot"}, {21, "Drill Sergeant"}, {22, "Boomer"}, {23, "Wrecking Ball"},
    {24, "Camo"}, {25, "Zook"}, {26, "Stealth Elf"}, {27, "Stump Smash"},
    {28, "Dark Spyro"}, {29, "Hex"}, {30, "Chop Chop"}, {31, "Ghost Roaster"},
    {32, "Cynder"},
    {100, "Jet-Vac"}, {101, "Swarm"}, {102, "Crusher"}, {103, "Flashwing"},
    {104, "Hot Head"}, {105, "Hot Dog"}, {106, "Chill"}, {107, "Thumpback"},
    {108, "Pop Fizz"}, {109, "Ninjini"}, {110, "Bouncer"}, {111, "Sprocket"},
    {112, "Tree Rex"}, {113, "Shroomboom"}, {114, "Eye-Brawl"}, {115, "Fright Rider"},
    {404, "Legendary Bash"}, {416, "Legendary Spyro"},
    {419, "Legendary Trigger Happy"}, {430, "Legendary Chop Chop"},
    {450, "Gusto"}, {451, "Thunderbolt"}, {452, "Fling Kong"}, {453, "Blades"},
    {454, "Wallop"}, {455, "Head Rush"}, {456, "Fist Bump"}, {457, "Rocky Roll"},
    {458, "Wildfire"}, {459, "Ka-Boom"}, {460, "Trail Blazer"}, {461, "Torch"},
    {462, "Snap Shot"}, {463, "Lob-Star"}, {464, "Flip Wreck"}, {465, "Echo"},
    {466, "Blastermind"}, {467, "Enigma"}, {468, "D\xc3\xa9j\xc3\xa0 Vu"}, {469, "Cobra Cadabra"},
    {470, "Jawbreaker"}, {471, "Gearshift"}, {472, "Chopper"}, {473, "Tread Head"},
    {474, "Bushwhack"}, {475, "Tuff Luck"}, {476, "Food Fight"}, {477, "High Five"},
    {478, "Krypt King"}, {479, "Short Cut"}, {480, "Bat Spin"}, {481, "Funny Bone"},
    {482, "Knight Light"}, {483, "Spotlight"}, {484, "Knight Mare"}, {485, "Blackout"},
    {601, "King Pen"}, {602, "Tri-Tip"}, {603, "Chopscotch"}, {604, "Boom Bloom"},
    {605, "Pit Boss"}, {606, "Barbella"}, {607, "Air Strike"}, {608, "Ember"},
    {609, "Ambush"}, {610, "Dr. Krankcase"}, {611, "Hood Sickle"},
    {612, "Taw Kwon Crow"}, {613, "Golden Queen"}, {614, "Wolfgang"},
    {615, "Pain-Yatta"}, {616, "Mysticat"}, {617, "Starcast"}, {618, "Buckshot"},
    {619, "Aurora"}, {620, "Flare Wolf"}, {621, "Chompy Mage"}, {622, "Bad Juju"},
    {623, "Grave Clobber"}, {624, "Blaster-Tron"}, {625, "Ro-Bow"},
    {626, "Chain Reaction"}, {627, "Kaos"}, {628, "Wild Storm"}, {629, "Tidepool"},
    {630, "Crash Bandicoot"}, {631, "Dr. Neo Cortex"},
    {3000, "Scratch"}, {3001, "Pop Thorn"}, {3002, "Slobber Tooth"},
    {3003, "Scorp"}, {3004, "Fryno"}, {3005, "Smolderdash"}, {3006, "Bumble Blast"},
    {3007, "Zoo Lou"}, {3008, "Dune Bug"}, {3009, "Star Strike"},
    {3010, "Countdown"}, {3011, "Wind-Up"}, {3012, "Roller Brawl"},
    {3013, "Grim Creeper"}, {3014, "Rip Tide"}, {3015, "Punk Shock"},
};
#define CHARACTER_IDS_COUNT (sizeof(CHARACTER_IDS) / sizeof(CHARACTER_IDS[0]))

/* POP_FIZZ_VARIANTS, ported verbatim from tools/portal_identify.py --
 * the only character this project has a real, captured variant table
 * for. Same source as CHARACTER_IDS. */
static const CharacterIdEntry POP_FIZZ_VARIANTS[] = {
    {4096, "Pop Fizz"},
    {4614, "Pop Fizz (LightCore)"},
    {5122, "Punch Pop Fizz"},
    {10245, "Super Gulp Pop Fizz"},
    {14341, "Fizzy Frenzy Pop Fizz"},
    {15362, "Love Potion Pop Fizz"},
};
#define POP_FIZZ_VARIANTS_COUNT (sizeof(POP_FIZZ_VARIANTS) / sizeof(POP_FIZZ_VARIANTS[0]))

const char *skylanders_figure_name(int32_t character_id) {
    size_t i;
    for (i = 0; i < CHARACTER_IDS_COUNT; i++) {
        if (CHARACTER_IDS[i].id == character_id) return CHARACTER_IDS[i].name;
    }
    return NULL;
}

const char *skylanders_figure_variant_name(int32_t character_id, int32_t variant_id) {
    size_t i;
    if (character_id != 108) return NULL; /* only Pop Fizz has a real variant table so far */
    for (i = 0; i < POP_FIZZ_VARIANTS_COUNT; i++) {
        if (POP_FIZZ_VARIANTS[i].id == variant_id) return POP_FIZZ_VARIANTS[i].name;
    }
    return NULL;
}

SkylandersFigureId skylanders_figure_decode_block1(const uint8_t block1[16]) {
    SkylandersFigureId out;
    out.character_id = (int32_t)((uint32_t)block1[0] | ((uint32_t)block1[1] << 8));
    out.variant_id = (int32_t)((uint32_t)block1[12] | ((uint32_t)block1[13] << 8));
    return out;
}

bool skylanders_figure_load_block(const char *path, uint32_t block_index, uint8_t block_out[16]) {
    FILE *f = fopen(path, "rb");
    size_t got;
    if (!f) return false;
    if (fseek(f, (long)block_index * 16L, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    got = fread(block_out, 1, 16, f);
    fclose(f);
    return got == 16;
}

bool skylanders_figure_identify_dump(const char *path, SkylandersFigureId *out) {
    uint8_t block1[16];
    if (!skylanders_figure_load_block(path, 1, block1)) return false;
    *out = skylanders_figure_decode_block1(block1);
    return true;
}
