#include "skylanders_nfc.h"

#include <string.h>
#include <switch.h>

#include "skylanders_mifare_keys.h"

/* See skylanders_nfc.h for the full real-flow explanation and the
 * honestly-flagged sector_number/block-index uncertainty. */

static bool g_nfc_initialized = false;

bool skylanders_nfc_init(void) {
    if (g_nfc_initialized) return true;
    if (R_FAILED(nfcMfInitialize())) return false;
    g_nfc_initialized = true;
    return true;
}

void skylanders_nfc_exit(void) {
    if (!g_nfc_initialized) return;
    nfcMfExit();
    g_nfc_initialized = false;
}

bool skylanders_nfc_read_figure(SkylandersFigureId *out) {
    NfcDeviceHandle handle;
    s32 total = 0;
    NfcMifareDeviceState state;
    int poll;
    NfcTagInfo tag_info;
    uint8_t uid[4];
    uint8_t key[6];
    NfcMifareReadBlockParameter read_param;
    NfcMifareReadBlockData read_data;
    bool ok;

    if (!g_nfc_initialized) return false;

    if (R_FAILED(nfcMfListDevices(&total, &handle, 1)) || total < 1) {
        return false; /* no NFC-capable controller found -- real, non-fatal */
    }

    if (R_FAILED(nfcMfStartDetection(&handle))) return false;

    /* Poll for a real tag for up to ~5 real seconds (50 * 100ms) --
     * matches skylanders_portal.c's own real-hardware-facing polling
     * style; a real figure being placed on a controller is a human-
     * timescale event, not something to busy-loop tightly for. */
    state = NfcMifareDeviceState_SearchingForTag;
    for (poll = 0; poll < 50; poll++) {
        if (R_FAILED(nfcMfGetDeviceState(&handle, &state))) {
            nfcMfStopDetection(&handle);
            return false;
        }
        if (state == NfcMifareDeviceState_TagFound || state == NfcMifareDeviceState_TagMounted) break;
        svcSleepThread(100000000ULL); /* 100ms, real nanosecond-resolution libnx sleep */
    }
    if (state != NfcMifareDeviceState_TagFound && state != NfcMifareDeviceState_TagMounted) {
        nfcMfStopDetection(&handle);
        return false; /* real timeout -- no figure placed in time */
    }

    ok = false;
    if (R_SUCCEEDED(nfcMfGetTagInfo(&handle, &tag_info)) && tag_info.uid.uid_length >= 4) {
        memcpy(uid, tag_info.uid.uid, 4);

        /* Block 1 (CharacterID/VariantID) is in MIFARE sector 0, which
         * always uses the fixed key regardless of this figure's own
         * UID -- see the header comment. */
        skylanders_mifare_key_a(0, uid, key);

        memset(&read_param, 0, sizeof(read_param));
        read_param.sector_number = 1; /* assumed absolute block index -- see header comment */
        read_param.sector_key.mifare_command = NfcMifareCommand_AuthA;
        read_param.sector_key.unknown = 1;
        memcpy(read_param.sector_key.sector_key, key, 6);

        memset(&read_data, 0, sizeof(read_data));
        if (R_SUCCEEDED(nfcMfReadMifare(&handle, &read_data, &read_param, 1))) {
            *out = skylanders_figure_decode_block1(read_data.data);
            ok = true;
        }
    }

    nfcMfStopDetection(&handle);
    return ok;
}
