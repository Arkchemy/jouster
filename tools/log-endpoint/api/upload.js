// Receives one run's log from the console and files it by build.
//
// The body is the gzipped log, sent as a PUT with no multipart wrapper, so
// this does not parse anything -- it hands the stream straight to Blob. That
// is why the console gzips first: this runtime caps a request body at about
// 4.5MB and a raw log is roughly eight.
//
// Authentication is a shared key in the query string rather than a header,
// because the console reads one URL out of one file on its SD card and that
// is the whole of its configuration. Without ARKCHEMY_KEY set, uploads are
// refused rather than left open -- an endpoint anyone can write to is one
// anyone can fill.

import { put, list, del } from '@vercel/blob';

export const config = { api: { bodyParser: false } };

// Keep the newest N logs and drop the rest.
//
// A console looping tests uploads one about every four minutes, so without
// this the store grows by roughly 40MB a day forever. Pruning happens after
// the upload succeeds and its failures are swallowed: losing an old log is a
// nuisance, losing the one that just arrived because tidying up failed is the
// thing this is supposed to prevent.
const KEEP = parseInt(process.env.ARKCHEMY_LOG_KEEP || '40', 10);

async function prune() {
  try {
    const { blobs } = await list({ prefix: 'logs/', limit: 1000 });
    if (blobs.length <= KEEP) return 0;
    const old = blobs
      .sort((a, b) => new Date(b.uploadedAt) - new Date(a.uploadedAt))
      .slice(KEEP);
    await del(old.map((b) => b.url));
    return old.length;
  } catch {
    return 0;
  }
}

export default async function handler(req, res) {
  if (req.method !== 'PUT' && req.method !== 'POST') {
    res.setHeader('Allow', 'PUT, POST');
    return res.status(405).json({ error: 'use PUT' });
  }

  const expected = process.env.ARKCHEMY_KEY;
  if (!expected) {
    return res.status(503).json({
      error: 'ARKCHEMY_KEY is not set on this deployment, so uploads are closed',
    });
  }
  const { key, build } = req.query;
  if (key !== expected) return res.status(401).json({ error: 'bad key' });

  // The build stamp decides where the log is filed, so a log is never an
  // unidentified version. It arrives from __DATE__ __TIME__ with spaces
  // already turned into underscores; anything else is not going in a path.
  const stamp = String(build || req.headers['x-arkchemy-build'] || 'unknown')
    .replace(/[^A-Za-z0-9_:.-]/g, '_')
    .slice(0, 64);
  const at = new Date().toISOString().replace(/[:.]/g, '-');
  const path = `logs/${stamp}/${at}.log.gz`;

  try {
    const blob = await put(path, req, {
      access: 'public',
      contentType: 'application/gzip',
      addRandomSuffix: false,
    });
    const pruned = await prune();
    return res.status(200).json({ ok: true, build: stamp, url: blob.url, pruned });
  } catch (err) {
    return res.status(500).json({ error: String(err && err.message || err) });
  }
}
