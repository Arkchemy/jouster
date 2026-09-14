# Log endpoint

Receives a hardware run's log from Jouster and files it by build, so a run
can be started anywhere and read from anywhere. The courier only works when
the Switch is sitting in hbmenu next to the machine running it; this is the
other half.

## Deploying

Needs a Vercel account and a Blob store. From this directory:

```bash
npm install
npx vercel link
npx vercel blob store add arkchemy-logs
npx vercel env add ARKCHEMY_KEY        # any long random string
npx vercel deploy --prod
```

`vercel blob store add` sets `BLOB_READ_WRITE_TOKEN` for you. `ARKCHEMY_KEY`
is the shared secret the console sends; **without it set, uploads are refused
rather than left open** — an endpoint anyone can write to is one anyone can
fill.

## Pointing the console at it

One file on the SD card, and nothing else:

```
sdmc:/switch/Jouster/upload-url.txt
https://<your-deployment>/api/upload?key=<ARKCHEMY_KEY>
```

No file means no upload, and the run's log says so. The URL is never compiled
into the binary, so a build handed to someone else does not phone home.

## Reading them back

```bash
curl "https://<your-deployment>/api/logs?format=text"
```

Newest first, one line each: time, size, build, URL. Fetch a URL and
`gunzip` it. Listing is open by default — the logs are recompiler diagnostics
and the store is write-protected — set `ARKCHEMY_LIST_KEY` to close it.

## Why gzipped

This runtime caps a request body at about 4.5MB and a raw log is around
eight. The console gzips before sending (`ark_gzip_log` in
`game/source/log_upload.c`); log text is repetitive enough that it lands far
under the cap. Uploading raw would mean every run silently rejected.

## What arrives

Blob path `logs/<build stamp>/<ISO timestamp>.log.gz`, where the build stamp
is the same `__DATE__ __TIME__` string the log's first line carries. The
stamp also travels in an `X-Arkchemy-Build` header and in the suggested
filename, so whichever end you are reading from, the version is there. An
evening was once lost to not knowing which binary produced which numbers.
