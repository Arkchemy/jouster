/*
 * See skylanders_portal.h for the big "this is unverified" disclaimer --
 * repeating the short version here since it matters for every line below:
 * written against real libnx headers and real community protocol
 * research, but never compiled or run (no devkitA64 toolchain reachable
 * in this sandbox, no real portal/Switch hardware to test against
 * either way). Expect real bugs on first real build+test.
 */
#include "skylanders_portal.h"

#include <string.h>
#include <switch.h>

#define PORTAL_VID 0x1430
#define PORTAL_PID 0x0150

static UsbHsClientIfSession g_if_session;
static UsbHsClientEpSession g_ep_in;
static UsbHsClientEpSession g_ep_out;
static uint16_t g_in_max_packet;
static uint16_t g_out_max_packet;
static bool g_connected = false;
static bool g_usbhs_initialized = false;

/* USB transfer buffers need to be allocated with real DMA-friendly
 * alignment on Switch -- 0x1000 (page-aligned) is the standard,
 * widely-used libnx convention for USB/GPU-facing buffers, not specific
 * knowledge about this device. Sized to the largest real packet this
 * device is documented to use (32 bytes, confirmed by the same
 * device-ID lookup source that gave the VID/PID), rounded up to a full
 * page since alignment matters far more than exact size for a buffer
 * this small. */
#define PORTAL_XFER_BUF_SIZE 0x1000
static uint8_t *g_xfer_buf = NULL;

static void portal_reset_session_state(void) {
    memset(&g_if_session, 0, sizeof(g_if_session));
    memset(&g_ep_in, 0, sizeof(g_ep_in));
    memset(&g_ep_out, 0, sizeof(g_ep_out));
    g_in_max_packet = 0;
    g_out_max_packet = 0;
    g_connected = false;
}

bool portal_init(void) {
    if (g_connected) return true; /* already open */

    if (!g_usbhs_initialized) {
        if (R_FAILED(usbHsInitialize())) return false;
        g_usbhs_initialized = true;
    }
    if (!g_xfer_buf) {
        g_xfer_buf = (uint8_t *)aligned_alloc(0x1000, PORTAL_XFER_BUF_SIZE);
        if (!g_xfer_buf) return false;
    }

    UsbHsInterfaceFilter filter;
    memset(&filter, 0, sizeof(filter));
    filter.Flags = UsbHsInterfaceFilterFlags_idVendor | UsbHsInterfaceFilterFlags_idProduct;
    filter.idVendor = PORTAL_VID;
    filter.idProduct = PORTAL_PID;

    /* A handful of candidate interfaces is plenty -- a real portal
     * exposes exactly one HID interface with 2 endpoints (confirmed via
     * the same device-ID lookup source as the VID/PID above); this
     * isn't trying to handle multiple portals attached at once. */
    UsbHsInterface iface_list[8];
    memset(iface_list, 0, sizeof(iface_list));
    s32 count = 0;
    Result rc = usbHsQueryAvailableInterfaces(&filter, iface_list, sizeof(iface_list), &count);
    if (R_FAILED(rc) || count <= 0) {
        /* No portal attached -- the normal, expected case when a game
         * is being played without one plugged in yet, not an error. */
        return false;
    }

    if (R_FAILED(usbHsAcquireUsbIf(&g_if_session, &iface_list[0]))) {
        return false;
    }

    /* Find the first real (nonzero bLength) input/output endpoint
     * descriptor -- real libnx layout has separate fixed-size
     * input_endpoint_descs[15]/output_endpoint_descs[15] arrays per
     * interface (see UsbHsInterfaceInfo in libnx's usbhs.h), only the
     * first slot(s) actually populated for a simple 2-endpoint device
     * like this one. */
    const UsbHsInterfaceInfo *inf = &iface_list[0].inf;
    const struct usb_endpoint_descriptor *in_desc = NULL;
    const struct usb_endpoint_descriptor *out_desc = NULL;
    for (int i = 0; i < 15; i++) {
        if (!in_desc && inf->input_endpoint_descs[i].bLength) in_desc = &inf->input_endpoint_descs[i];
        if (!out_desc && inf->output_endpoint_descs[i].bLength) out_desc = &inf->output_endpoint_descs[i];
    }
    if (!in_desc || !out_desc) {
        usbHsIfClose(&g_if_session);
        portal_reset_session_state();
        return false;
    }

    if (R_FAILED(usbHsIfOpenUsbEp(&g_if_session, &g_ep_in, 1, in_desc->wMaxPacketSize,
                                   (struct usb_endpoint_descriptor *)in_desc))) {
        usbHsIfClose(&g_if_session);
        portal_reset_session_state();
        return false;
    }
    if (R_FAILED(usbHsIfOpenUsbEp(&g_if_session, &g_ep_out, 1, out_desc->wMaxPacketSize,
                                   (struct usb_endpoint_descriptor *)out_desc))) {
        usbHsEpClose(&g_ep_in);
        usbHsIfClose(&g_if_session);
        portal_reset_session_state();
        return false;
    }

    g_in_max_packet = in_desc->wMaxPacketSize;
    g_out_max_packet = out_desc->wMaxPacketSize;
    g_connected = true;
    return true;
}

void portal_shutdown(void) {
    if (g_connected) {
        usbHsEpClose(&g_ep_in);
        usbHsEpClose(&g_ep_out);
        usbHsIfClose(&g_if_session);
    }
    portal_reset_session_state();
    if (g_xfer_buf) {
        free(g_xfer_buf);
        g_xfer_buf = NULL;
    }
    if (g_usbhs_initialized) {
        usbHsExit();
        g_usbhs_initialized = false;
    }
}

bool portal_is_connected(void) { return g_connected; }

bool portal_write_command(const uint8_t *cmd, size_t len) {
    if (!g_connected || !g_xfer_buf) return false;
    if (len > g_out_max_packet) return false; /* real packet can't exceed the endpoint's max size */

    memset(g_xfer_buf, 0, g_out_max_packet);
    memcpy(g_xfer_buf, cmd, len);

    u32 transferred = 0;
    Result rc = usbHsEpPostBuffer(&g_ep_out, g_xfer_buf, g_out_max_packet, &transferred);
    return R_SUCCEEDED(rc) && transferred == g_out_max_packet;
}

bool portal_read_response(uint8_t *out, size_t out_len) {
    if (!g_connected || !g_xfer_buf) return false;

    u32 transferred = 0;
    Result rc = usbHsEpPostBuffer(&g_ep_in, g_xfer_buf, g_in_max_packet, &transferred);
    if (R_FAILED(rc)) return false;

    size_t copy_len = (out_len < transferred) ? out_len : transferred;
    memcpy(out, g_xfer_buf, copy_len);
    if (copy_len < out_len) memset(out + copy_len, 0, out_len - copy_len);
    return true;
}

bool portal_set_color(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t cmd[4] = {0x43, r, g, b};
    return portal_write_command(cmd, sizeof(cmd));
}
