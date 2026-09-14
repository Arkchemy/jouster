/* Update the app over wifi, so the console does not have to be plugged in.
 *
 * At startup: ask the endpoint what the newest build is, compare it with the
 * one running, and if they differ download it, replace the NRO on the card and
 * chain-load it. The courier still works and is faster on the same desk; this
 * is for when the Switch is anywhere else.
 *
 * The identity is the build stamp -- the same __DATE__ __TIME__ string the log
 * prints on its first line -- in its filename-safe form, because that string
 * has to survive a URL path unchanged. A stamp that changes shape in transit
 * is a stamp that never matches, and an updater that thinks every build is new
 * downloads 170MB on every launch forever.
 *
 * Three things this deliberately does not do:
 *
 *   - It does not update itself more than once per launch. If a download
 *     somehow produces a binary whose stamp still differs, that is a loop, and
 *     a loop that downloads 170MB a go is worse than being out of date.
 *   - It does not replace the NRO until the whole file is on the card and its
 *     size matches what the manifest promised. A half-downloaded NRO is a
 *     console that will not boot the app at all.
 *   - It does not run at all without sdmc:/switch/Jouster/update-url.txt. No
 *     build of this fetches code from the network unless someone put an
 *     address on the card by hand.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <switch.h>
#include <curl/curl.h>

#define ARK_UPDATE_URL_FILE "sdmc:/switch/Jouster/update-url.txt"
#define ARK_SELF_NRO        "sdmc:/switch/Jouster.nro"
#define ARK_SELF_NEW        "sdmc:/switch/Jouster.nro.new"
#define ARK_UPDATE_MARK     "sdmc:/switch/Jouster/.updated-this-boot"

/* The build stamp, as one contiguous string in the binary.
 *
 * publish-build.sh reads this back out of the built NRO rather than being
 * told what it is publishing, so the name a build is published under cannot
 * disagree with the build that will actually run. It has to be one string
 * literal for `strings` to find it -- assembling it at runtime from __DATE__
 * and __TIME__ leaves two separate fragments in .rodata and nothing to grep.
 * `used` keeps the linker from discarding what nothing references. */
__attribute__((used))
const char ark_build_marker[] = "ARKCHEMY_BUILD " __DATE__ " " __TIME__;


/* __DATE__ __TIME__ in the form a URL path can carry: "Sep 13 2026 10:56:43"
 * becomes "Sep_13_2026_10-56-43". Must match publish-build.sh exactly. */
static void ark_stamp_safe(char *out, size_t cap)
{
    /* Derived from the marker rather than from __DATE__/__TIME__ again, for
     * two reasons: it makes the marker a referenced symbol so --gc-sections
     * cannot drop it (which it silently did on the first attempt, leaving
     * publish-build.sh with nothing to read), and it makes it impossible for
     * the string published and the string compared to ever differ. */
    snprintf(out, cap, "%s", ark_build_marker + sizeof("ARKCHEMY_BUILD ") - 1);
    for (char *p = out; *p; p++) {
        if (*p == ' ') *p = '_';
        else if (*p == ':') *p = '-';
    }
}

struct membuf { char *p; size_t n; };

static size_t ark_collect(void *data, size_t sz, size_t nm, void *user)
{
    struct membuf *m = user;
    size_t add = sz * nm;
    if (m->n + add > 4000) return 0;
    memcpy(m->p + m->n, data, add);
    m->n += add;
    m->p[m->n] = '\0';
    return add;
}

/* One field out of the manifest's text form, which is deliberately flat
 * "key=value" lines so this needs no JSON parser. */
static bool ark_field(const char *body, const char *key, char *out, size_t cap)
{
    const char *at = strstr(body, key);
    if (!at) return false;
    at += strlen(key);
    size_t i = 0;
    while (*at && *at != '\n' && *at != '\r' && i + 1 < cap) out[i++] = *at++;
    out[i] = '\0';
    return i > 0;
}

static bool ark_read_file_line(const char *path, char *out, size_t cap)
{
    FILE *fh = fopen(path, "rb");
    if (!fh) return false;
    size_t n = fread(out, 1, cap - 1, fh);
    fclose(fh);
    out[n] = '\0';
    while (n && (out[n-1] == '\n' || out[n-1] == '\r' || out[n-1] == ' ')) out[--n] = '\0';
    return n > 0 && strncmp(out, "http", 4) == 0;
}

int arkchemy_self_update(void (*report)(const char *fmt, ...))
{
    char base[512];
    if (!ark_read_file_line(ARK_UPDATE_URL_FILE, base, sizeof(base))) {
        report("UPDATE off -- no %s on the card", ARK_UPDATE_URL_FILE);
        return 0;
    }

    /* One attempt per boot, marked on the card rather than in memory: the
     * whole point is that the next thing to run is a different binary. */
    if (access(ARK_UPDATE_MARK, F_OK) == 0) {
        remove(ARK_UPDATE_MARK);
        report("UPDATE already applied this boot -- not checking again");
        return 0;
    }

    if (socketInitializeDefault() != 0) { report("UPDATE skipped -- no sockets"); return 0; }
    CURL *c = curl_easy_init();
    if (!c) { socketExit(); report("UPDATE skipped -- curl would not start"); return 0; }

    char url[640], body[4096] = {0};
    struct membuf m = { body, 0 };
    snprintf(url, sizeof(url), "%s%sapp=jouster&format=text", base,
             strchr(base, '?') ? "&" : "?");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, ark_collect);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        socketExit();
        report("UPDATE check failed -- %s", curl_easy_strerror(rc));
        return 0;
    }

    char want[64], size_s[32], nro_url[768], mine[64];
    ark_stamp_safe(mine, sizeof(mine));
    if (!ark_field(body, "build=", want, sizeof(want))
        || !ark_field(body, "size=", size_s, sizeof(size_s))
        || !ark_field(body, "url=", nro_url, sizeof(nro_url))) {
        socketExit();
        report("UPDATE check failed -- manifest did not parse");
        return 0;
    }
    if (strcmp(want, mine) == 0) {
        socketExit();
        report("UPDATE none -- running the newest build (%s)", mine);
        return 0;
    }

    long want_size = atol(size_s);
    report("UPDATE available -- %s replaces %s, %ld bytes, downloading",
           want, mine, want_size);

    /* Resume rather than restart.
     *
     * The first version downloaded in one attempt and a console reported
     * exactly what you would expect of 170MB over console wifi: "Transferred
     * a partial file", and 170MB thrown away. Starting again from zero on
     * every drop means a flaky link never finishes at all, however long you
     * leave it. Each attempt picks up from whatever is already on the card,
     * so a run of bad luck costs time rather than progress. */
    long have = 0;
    FILE *chk0 = fopen(ARK_SELF_NEW, "rb");
    if (chk0) { fseek(chk0, 0, SEEK_END); have = ftell(chk0); fclose(chk0); }
    if (want_size > 0 && have > want_size) { remove(ARK_SELF_NEW); have = 0; }

    rc = CURLE_OK;
    for (int attempt = 1; attempt <= 5; attempt++) {
        if (want_size > 0 && have >= want_size) break;

        FILE *out = fopen(ARK_SELF_NEW, have ? "ab" : "wb");
        if (!out) { socketExit(); report("UPDATE failed -- cannot write %s", ARK_SELF_NEW); return 0; }

        c = curl_easy_init();
        curl_easy_setopt(c, CURLOPT_URL, nro_url);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 1800L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        if (have > 0) curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)have);
        /* Give up on a stalled socket instead of sitting on it for the full
         * timeout: under 4KB/s for 60s is not a download that is going to
         * finish, and the next attempt may get a better connection. */
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 4096L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
        rc = curl_easy_perform(c);
        curl_easy_cleanup(c);
        fclose(out);

        FILE *now = fopen(ARK_SELF_NEW, "rb");
        long grew = 0;
        if (now) { fseek(now, 0, SEEK_END); grew = ftell(now); fclose(now); }

        if (rc == CURLE_OK && (want_size <= 0 || grew >= want_size)) { have = grew; break; }
        if (grew <= have) {
            /* No progress at all this attempt -- the server may not support
             * ranges, in which case retrying from the same offset is futile. */
            report("UPDATE attempt %d made no progress (%s) -- starting over",
                   attempt, curl_easy_strerror(rc));
            remove(ARK_SELF_NEW);
            have = 0;
        } else {
            report("UPDATE attempt %d reached %ld of %ld bytes (%s) -- resuming",
                   attempt, grew, want_size, curl_easy_strerror(rc));
            have = grew;
        }
        svcSleepThread(3000000000ULL);
    }
    socketExit();

    if (want_size > 0 && have < want_size) {
        report("UPDATE failed -- stopped at %ld of %ld bytes after 5 attempts."
               " The partial file is kept, so the next launch resumes it."
               " Still running %s", have, want_size, mine);
        return 0;
    }

    /* Size is the only integrity check available without hashing 170MB on a
     * console that has a run to get on with -- but it does catch the failure
     * that actually happens, which is a truncated download. */
    FILE *chk = fopen(ARK_SELF_NEW, "rb");
    long got = 0;
    if (chk) { fseek(chk, 0, SEEK_END); got = ftell(chk); fclose(chk); }
    if (want_size > 0 && got != want_size) {
        remove(ARK_SELF_NEW);
        report("UPDATE failed -- got %ld bytes, expected %ld. Still running %s",
               got, want_size, mine);
        return 0;
    }

    remove(ARK_SELF_NRO);
    if (rename(ARK_SELF_NEW, ARK_SELF_NRO) != 0) {
        report("UPDATE failed -- could not replace %s", ARK_SELF_NRO);
        return 0;
    }

    FILE *mark = fopen(ARK_UPDATE_MARK, "wb");
    if (mark) { fputs(want, mark); fclose(mark); }

    report("UPDATE installed %s (%ld bytes) -- chain-loading it now", want, got);
    envSetNextLoad(ARK_SELF_NRO, "\"" ARK_SELF_NRO "\"");
    return 1;   /* caller must return from main so hbmenu loads it */
}

/* Whether this console should start another run when this one ends.
 *
 * Two files, both on the card, because the console may be nowhere near a
 * keyboard when you change your mind:
 *
 *   loop.txt  -- opt in. Without it a run ends the way it always did.
 *   stop.txt  -- opt out, and it wins. Dropping this on the card (the courier
 *                can push it) breaks the loop after the current run, rather
 *                than needing someone to hold a button at the right moment.
 *
 * The loop is deliberately not a loop inside one process. Each cycle is a
 * fresh launch through envSetNextLoad, so every run starts from the same
 * clean state as a hand-launched one -- a rig whose runs drift from real runs
 * stops meaning anything, and this project has spent enough evenings on
 * numbers that turned out to describe something other than what it thought.
 */
int arkchemy_loop_continue(void (*report)(const char *fmt, ...))
{
    if (access("sdmc:/switch/Jouster/loop.txt", F_OK) != 0) return 0;
    if (access("sdmc:/switch/Jouster/stop.txt", F_OK) == 0) {
        report("LOOP stopping -- stop.txt is on the card");
        return 0;
    }

    long n = 0;
    FILE *f = fopen("sdmc:/switch/Jouster/loop-count.txt", "rb");
    if (f) { if (fscanf(f, "%ld", &n) != 1) n = 0; fclose(f); }
    n++;
    f = fopen("sdmc:/switch/Jouster/loop-count.txt", "wb");
    if (f) { fprintf(f, "%ld\n", n); fclose(f); }

    /* A newer build takes priority: hand over to that rather than to this. */
    if (arkchemy_self_update(report)) {
        report("LOOP cycle %ld done -- newer build installed, handing over", n);
        return 1;
    }

    /* Park in hbmenu every few cycles.
     *
     * Chain-loading is instant, so a looping console is never in hbmenu, and
     * MTP only comes up in hbmenu -- which means the courier can never push a
     * new build or pull a log while the loop runs. On a console with no
     * network that is the whole delivery mechanism, so an unbroken loop would
     * re-run one build forever and nothing new could ever reach it.
     *
     * Every PARK_EVERY cycles it stops instead of relaunching. The console
     * sits in hbmenu, the courier gets its window, and one launch starts the
     * next batch. Set loop-park.txt to a number to change the count. */
    long park = 5;
    FILE *pf = fopen("sdmc:/switch/Jouster/loop-park.txt", "rb");
    if (pf) { if (fscanf(pf, "%ld", &park) != 1) park = 5; fclose(pf); }
    if (park > 0 && (n % park) == 0) {
        report("LOOP cycle %ld done -- stopping so a new build can be moved"
               " over. MTP only comes up while the console is running its USB"
               " transfer app (Haze/DBI), not in hbmenu and not during a run,"
               " so the transfer needs that app opened by hand. Launch Jouster"
               " again afterwards to continue", n);
        return 0;
    }

    /* Breathing room. If something makes the app fail instantly, this keeps
     * the console out of a launch storm and leaves a window to drop stop.txt
     * on the card. */
    svcSleepThread(5000000000ULL);

    report("LOOP cycle %ld done -- relaunching the same build", n);
    envSetNextLoad(ARK_SELF_NRO, "\"" ARK_SELF_NRO "\"");
    return 1;
}


/* One line per run, so a build's outcome is a rate rather than an anecdote.
 *
 * This exists because of what the 2026-09-13/14 loop cycles showed: the same
 * binary, relaunched unchanged, completed seven runs and failed five, and the
 * failure was indistinguishable from the one eight builds had been bisected
 * against. Every conclusion drawn from a single run of this rig since the draw
 * path went in is worth exactly as much as a coin toss, and there was no way
 * to see that from inside a run -- each one looks definitive on its own.
 *
 * The trick is that a failed run cannot file its own report: it never reaches
 * the end of main, which is the whole definition of failing. So the record is
 * opened at startup and closed at exit, and it is the *next* run that notices
 * an open record nobody closed and writes the failure down. A run that dies
 * twice in a row still only loses one line, which is the right trade -- the
 * alternative is a rig that under-reports exactly the failure it exists to
 * count.
 */
#define ARK_TALLY      "sdmc:/switch/Jouster/run-tally.txt"
#define ARK_TALLY_OPEN "sdmc:/switch/Jouster/run-open.txt"

void arkchemy_run_tally_open(void (*report)(const char *fmt, ...))
{
    char prev[256] = {0};
    FILE *o = fopen(ARK_TALLY_OPEN, "rb");
    if (o) {
        size_t n = fread(prev, 1, sizeof(prev) - 1, o);
        prev[n] = '\0';
        fclose(o);
        remove(ARK_TALLY_OPEN);

        FILE *t = fopen(ARK_TALLY, "a");
        if (t) { fprintf(t, "FAIL  %s\n", prev); fclose(t); }
        report("TALLY previous run never finished (%s) -- recorded as FAIL", prev);
    }

    /* What the next run needs to describe this one if it does not come back.
     * The build stamp is the point: a tally that cannot say which build a
     * failure belongs to cannot be used to compare two builds, which is the
     * only thing it is for. */
    char mine[64];
    ark_stamp_safe(mine, sizeof(mine));
    FILE *n = fopen(ARK_TALLY_OPEN, "wb");
    if (n) { fprintf(n, "build=%s", mine); fclose(n); }
}

void arkchemy_run_tally_close(void (*report)(const char *fmt, ...),
                              int frames, unsigned draws, unsigned modules)
{
    char mine[64];
    ark_stamp_safe(mine, sizeof(mine));

    FILE *t = fopen(ARK_TALLY, "a");
    if (t) {
        fprintf(t, "OK    build=%s frames=%d draws=%u modules=%u\n",
                mine, frames, draws, modules);
        fclose(t);
    }
    remove(ARK_TALLY_OPEN);
    report("TALLY ok -- frames=%d draws=%u modules=%u", frames, draws, modules);
}
