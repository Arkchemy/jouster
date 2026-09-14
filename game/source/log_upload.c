/* Send the run's log somewhere it can be read without the SD card.
 *
 * The courier only works when the Switch is sitting in hbmenu on the same
 * desk as the machine running it. This is the other half: if the console has
 * internet, the log goes to an endpoint at the end of the run, so a run can
 * be started anywhere and read anywhere.
 *
 * Two rules shape this:
 *
 * **The destination is not compiled in.** It is read from
 * sdmc:/switch/Jouster/upload-url.txt. No file, no upload, and the log says
 * so. A hardcoded URL would mean every build of this project quietly ships
 * something that phones home, including builds handed to someone else.
 *
 * **The build identity travels with the log.** A log whose version you have
 * to guess at is worth very little -- most of an evening was once spent
 * wondering which binary produced which numbers. The build stamp goes in the
 * headers and in the upload's filename, not only in the body.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <switch.h>
#include <curl/curl.h>
#include <zlib.h>

#define ARK_UPLOAD_URL_FILE "sdmc:/switch/Jouster/upload-url.txt"
#define ARK_LOG_FILE        "sdmc:/switch/Jouster/game-results.log"
#define ARK_LOG_GZ          "sdmc:/switch/Jouster/game-results.log.gz"

/* The build stamp -- taken from self_update.c's marker, not rebuilt from
 * __DATE__ here.
 *
 * Two files compiled at different moments give two different stamps, and a
 * log then arrives filed under a build that never existed: a console reported
 * "UPDATE ... Still running Sep_13_2026_11-09-49" inside a log the endpoint
 * had filed as 10:56:43. One binary must have exactly one identity. */
extern const char ark_build_marker[];

static const char *ark_build_stamp(void)
{
    return ark_build_marker + sizeof("ARKCHEMY_BUILD ") - 1;
}

/* Whether this console can actually reach anything. nifm knows; trying a
 * connect and waiting for it to time out does not, and a timeout at the end
 * of a run looks exactly like a hang. */
static bool ark_have_internet(void)
{
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) return false;
    NifmInternetConnectionType type;
    u32 strength = 0;
    NifmInternetConnectionStatus status;
    Result rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
    nifmExit();
    return R_SUCCEEDED(rc) && status == NifmInternetConnectionStatus_Connected;
}

/* Read the endpoint, trimmed of whitespace and any trailing newline the
 * editor left behind. Returns false when the file is absent, which is the
 * ordinary case and not an error. */
static bool ark_read_url(char *out, size_t cap)
{
    FILE *fh = fopen(ARK_UPLOAD_URL_FILE, "rb");
    if (!fh) return false;
    size_t n = fread(out, 1, cap - 1, fh);
    fclose(fh);
    out[n] = '\0';
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '
                 || out[n - 1] == '\t'))
        out[--n] = '\0';
    return n > 0 && strncmp(out, "http", 4) == 0;
}

/* Compress the log before sending it.
 *
 * Not an optimisation. A serverless endpoint -- Vercel's, among others --
 * caps a request body at a few megabytes, and these logs run to eight; a run
 * that uploads nothing because the body was rejected is worse than one that
 * never tried. Log text is extremely repetitive (the same probe lines a few
 * hundred times over), so gzip takes it to a small fraction of that.
 *
 * Returns the compressed size, or 0 if anything went wrong -- in which case
 * the caller sends nothing rather than sending a truncated file. */
static long ark_gzip_log(void)
{
    FILE *in = fopen(ARK_LOG_FILE, "rb");
    if (!in) return 0;
    gzFile out = gzopen(ARK_LOG_GZ, "wb6");
    if (!out) { fclose(in); return 0; }

    static char buf[32 * 1024];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (gzwrite(out, buf, (unsigned)n) != (int)n) { ok = false; break; }
    }
    fclose(in);
    if (gzclose(out) != Z_OK) ok = false;
    if (!ok) { remove(ARK_LOG_GZ); return 0; }

    FILE *gz = fopen(ARK_LOG_GZ, "rb");
    if (!gz) return 0;
    fseek(gz, 0, SEEK_END);
    long len = ftell(gz);
    fclose(gz);
    return len;
}

/* Upload the log as the request body. PUT rather than a multipart POST: the
 * body is then exactly the file, which any endpoint from a one-line script to
 * an object store can accept without parsing anything. */
void arkchemy_upload_log(void (*report)(const char *fmt, ...))
{
    char url[512];
    if (!ark_read_url(url, sizeof(url))) {
        report("UPLOAD off -- no %s on the card. Put a URL in that file and"
               " every run will send its log there at exit",
               ARK_UPLOAD_URL_FILE);
        return;
    }
    if (!ark_have_internet()) {
        report("UPLOAD skipped -- no internet connection reported by nifm");
        return;
    }

    long len = ark_gzip_log();
    if (len <= 0) { report("UPLOAD skipped -- could not compress the log"); return; }
    FILE *fh = fopen(ARK_LOG_GZ, "rb");
    if (!fh) { report("UPLOAD skipped -- no log to send"); return; }

    if (socketInitializeDefault() != 0) {
        fclose(fh);
        report("UPLOAD failed -- sockets would not initialise");
        return;
    }

    CURL *c = curl_easy_init();
    if (!c) {
        fclose(fh);
        socketExit();
        report("UPLOAD failed -- curl would not initialise");
        return;
    }

    /* The stamp travels three ways, because whichever end you are reading
     * from, one of them is the convenient one: in the URL, in a header, and
     * already in the log's own first line. */
    char full[640], stamp_hdr[96], name_hdr[160];
    snprintf(full, sizeof(full), "%s%sbuild=%s", url,
             strchr(url, '?') ? "&" : "?", ark_build_stamp());
    for (char *p = full; *p; p++) if (*p == ' ') *p = '_';
    snprintf(stamp_hdr, sizeof(stamp_hdr), "X-Arkchemy-Build: %s", ark_build_stamp());
    snprintf(name_hdr, sizeof(name_hdr),
             "Content-Disposition: attachment; filename=\"jouster-%s.log.gz\"",
             ark_build_stamp());
    for (char *p = name_hdr; *p; p++) if (*p == ' ' && p > name_hdr + 40) *p = '_';

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/gzip");
    headers = curl_slist_append(headers, stamp_hdr);
    headers = curl_slist_append(headers, name_hdr);

    curl_easy_setopt(c, CURLOPT_URL, full);
    curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(c, CURLOPT_READDATA, fh);
    curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, (curl_off_t)len);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "arkchemy-jouster/1.0");
    /* A run that has already finished must not be held open by a dead
     * endpoint: fail and say so rather than sit there. */
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 180L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);

    if (rc == CURLE_OK && http >= 200 && http < 300)
        report("UPLOAD sent %ld bytes gzipped, build %s, HTTP %ld",
               len, ark_build_stamp(), http);
    else
        report("UPLOAD failed -- %s (HTTP %ld). The log is still on the card",
               curl_easy_strerror(rc), http);

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    fclose(fh);
    socketExit();
}
