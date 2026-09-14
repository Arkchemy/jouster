// Lists what has been uploaded, newest first: build, time, size, URL.
//
// Deliberately readable without the key. The logs are diagnostic output from
// a recompiler, the store is write-protected by ARKCHEMY_KEY, and having to
// authenticate to answer "did that run upload?" is friction with no benefit.
// Set ARKCHEMY_LIST_KEY if that ever stops being true.

import { list } from '@vercel/blob';

export default async function handler(req, res) {
  const gate = process.env.ARKCHEMY_LIST_KEY;
  if (gate && req.query.key !== gate) return res.status(401).json({ error: 'bad key' });

  const limit = Math.min(parseInt(req.query.limit || '50', 10) || 50, 500);
  try {
    const { blobs } = await list({ prefix: 'logs/', limit });
    const rows = blobs
      .map((b) => ({
        build: b.pathname.split('/')[1] || 'unknown',
        uploadedAt: b.uploadedAt,
        size: b.size,
        url: b.url,
      }))
      .sort((a, b) => new Date(b.uploadedAt) - new Date(a.uploadedAt));

    if (req.query.format === 'text') {
      res.setHeader('Content-Type', 'text/plain; charset=utf-8');
      return res.status(200).send(
        rows.map((r) => `${r.uploadedAt}  ${String(r.size).padStart(9)}  ${r.build}  ${r.url}`).join('\n') + '\n'
      );
    }
    return res.status(200).json({ count: rows.length, logs: rows });
  } catch (err) {
    return res.status(500).json({ error: String(err && err.message || err) });
  }
}
