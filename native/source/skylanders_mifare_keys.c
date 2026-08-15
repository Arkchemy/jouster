#include "skylanders_mifare_keys.h"

/* Real CRC-48 variant used by skylandersNFC/SkyKeys-Generator's real
 * getKeyA algorithm -- ported directly from that project's script.js
 * (computeCRC48), not derived independently. Uses the real, standard
 * CRC-64/XZ polynomial (0x42f0e1eba9ea3693) truncated to 48 bits, with
 * an unusual fixed initial register value the original JS computes as
 * `2 * 2 * 3 * 1103 * 12868356821` -- kept as the same literal
 * multiplication here (rather than pre-folded into one constant) so a
 * byte-for-byte diff against the original source stays easy. */
static uint64_t compute_crc48(const uint8_t *data, int len) {
    const uint64_t polynomial = 0x42f0e1eba9ea3693ULL;
    uint64_t reg = (uint64_t)2 * 2 * 3 * 1103 * 12868356821ULL;
    int i, j;
    for (i = 0; i < len; i++) {
        reg ^= ((uint64_t)data[i]) << 40;
        for (j = 0; j < 8; j++) {
            if (reg & 0x800000000000ULL) {
                reg = (reg << 1) ^ polynomial;
            } else {
                reg <<= 1;
            }
            reg &= 0x0000FFFFFFFFFFFFULL;
        }
    }
    return reg;
}

void skylanders_mifare_key_a(int sector, const uint8_t uid[4], uint8_t key_out[6]) {
    int i;
    if (sector < 0 || sector > 15) sector = 0; /* defensive clamp -- see header comment */

    if (sector == 0) {
        /* Real fixed key for sector 0, per the original JS:
         * `(73 * 2017 * 560381651).toString(16)` -- note this is NOT
         * byte-reversed the way the CRC48-derived sectors 1-15 below
         * are (a real, confirmed asymmetry in the original algorithm,
         * not a copy-paste inconsistency in this port -- an earlier
         * draft of this port got this wrong by applying the same
         * byte-reversal uniformly, caught by the side-by-side
         * verification against the real original JS, not by
         * inspection). */
        uint64_t k = (uint64_t)73 * 2017 * 560381651ULL;
        for (i = 0; i < 6; i++) key_out[i] = (uint8_t)(k >> (8 * (5 - i)));
        return;
    }

    {
        uint8_t buf[5];
        uint64_t crc;
        buf[0] = uid[0]; buf[1] = uid[1]; buf[2] = uid[2]; buf[3] = uid[3];
        buf[4] = (uint8_t)sector;
        crc = compute_crc48(buf, 5);
        /* Real byte-reversal, matching the original JS's
         * `.toString(16).padStart(12,"0").match(/.{1,2}/g).reverse().join("")`
         * -- the CRC register's big-endian hex representation, byte-pair
         * reversed into little-endian order. */
        for (i = 0; i < 6; i++) key_out[i] = (uint8_t)(crc >> (8 * i));
    }
}
