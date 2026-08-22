#ifndef ARKCHEMY_SKYLANDERS_PORTAL_H
#define ARKCHEMY_SKYLANDERS_PORTAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Real USB host communication with a Skylanders Portal of Power,
 * attached either via the dock's USB ports or a USB-C hub/adapter in
 * handheld mode -- both go through the same libnx usb:hs (USB host) API,
 * there's no separate code path for the two.
 *
 * ***UNVERIFIED*** -- flagged explicitly, unlike everything else in this
 * project's shim work, which was directly tested before being committed.
 * No devkitA64 toolchain was reachable in this sandbox session (needed
 * for the real libnx <switch.h>/usbhs.h), and there is no real Portal of
 * Power or real Switch hardware available to test against either way.
 * What *was* actually done: the .c file was compiled clean against a
 * hand-written stub header matching every real struct field name/layout
 * and function signature fetched directly from switchbrew/libnx's
 * current usbhs.h/usb.h (not paraphrased) -- confirms no syntax errors
 * and correct API call shapes, nothing about real USB/hardware
 * semantics. This is a first draft that needs a real build and real
 * hardware testing before it can be trusted -- expect to find and fix
 * real bugs.
 *
 * Sources used, and confidence level:
 *  - Real, high confidence: the Portal's USB vendor/product ID
 *    (0x1430:0x0150 -- corroborated across multiple independent
 *    community sources: a device-ID lookup site, the Cemu wiki's own
 *    Skylanders Portal FAQ, and a GitHub issue against an open-source
 *    portal-communication library), and libnx's real usbhs.h function
 *    signatures / struct field names (fetched directly from
 *    switchbrew/libnx's actual current header, not paraphrased).
 *  - Real but less certain: the portal's own command byte format (0x43
 *    = set RGB color, 0x51 = query an RFID tag, 0x52 = reset) is from
 *    DamonOehlman/skyportal, an open-source Node.js library that talks
 *    to real portals -- real, but that source didn't show the exact
 *    packet framing (whether there's a leading report-ID byte, exact
 *    total packet length beyond "32 bytes max", or the RFID response
 *    format), so packet framing below is a reasonable inference, not a
 *    confirmed byte-for-byte match.
 */

/* Attempts to find and open a real, physically attached Portal of Power
 * over USB. Returns false if libnx's USB host service can't be reached,
 * or (much more commonly, and not an error) no portal is currently
 * attached -- this is a normal, expected outcome (a Skylanders game
 * played without a portal connected yet), not a fatal condition. */
bool portal_init(void);

/* Releases the USB interface/endpoints. Safe to call even if portal_init
 * never found a device. */
void portal_shutdown(void);

/* True once portal_init has successfully opened a real device and it
 * hasn't been closed since. */
bool portal_is_connected(void);

/* Sets the portal's ring light color. Real command format: a single
 * packet, first byte 0x43, followed by R, G, B (one byte each),
 * zero-padded to the endpoint's real max packet size. */
bool portal_set_color(uint8_t r, uint8_t g, uint8_t b);

/* Blocking write of a raw command packet to the portal's OUT endpoint,
 * zero-padded to the endpoint's max packet size. Returns false on any
 * USB transfer failure (including "no portal attached"). */
bool portal_write_command(const uint8_t *cmd, size_t len);

/* Blocking read of one response packet from the portal's IN endpoint
 * into `out` (up to `out_len` bytes, truncated if the real packet is
 * larger). Returns false on any USB transfer failure. */
bool portal_read_response(uint8_t *out, size_t out_len);

#endif /* ARKCHEMY_SKYLANDERS_PORTAL_H */
