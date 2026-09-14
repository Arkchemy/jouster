// What the newest build of each app is, and where to get it.
//
// The console asks this at startup and compares the answer with its own
// __DATE__ __TIME__ stamp. That comparison is why builds are filed under the
// stamp rather than a version number: there is no release process here, only
// a compiler, and the stamp is the one identifier that already exists on both
// ends without anybody maintaining it.
//
// Ordering is by upload time, not by the stamp. Stamps are strings like
// "Sep 13 2026 10:56:43" and sorting those lexically puts December before
// February; the store already knows when each blob arrived.

import { list } from '@vercel/blob';

const GH_RAW = process.env.ARKCHEMY_GH_RAW
  || 'https://raw.githubusercontent.com/Arkchemy/jouster/main/builds';

async function fromGithub(app) {
  try {
    const r = await fetch(`${GH_RAW}/${encodeURIComponent(app)}.txt`, {
      cache: 'no-store',
    });
    if (!r.ok) return null;
    const text = await r.text();
    const field = (k) => (text.match(new RegExp(`^${k}=(.*)$`, 'm')) || [])[1];
    const build = field('build');
    const url = field('url');
    if (!build || !url) return null;
    return {
      app,
      build,
      size: parseInt(field('size') || '0', 10),
      url,
      uploadedAt: new Date().toISOString(),
      via: 'github',
    };
  } catch {
    return null;
  }
}

export default async function handler(req, res) {
  const want = String(req.query.app || '').toLowerCase();
  try {
    const { blobs } = await list({ prefix: 'builds/', limit: 1000 });

    const latest = {};
    for (const b of blobs) {
      // builds/<app>/<stamp>.nro
      const parts = b.pathname.split('/');
      if (parts.length !== 3 || !parts[2].endsWith('.nro')) continue;
      const app = parts[1];
      // Kept exactly as the filename spells it. The console computes the
      // same filename-safe form of its own __DATE__ __TIME__ and compares
      // strings: "Sep 13 2026 10:56:43" cannot survive a path, and a stamp
      // that changes shape in transit is a stamp that never matches, which
      // would make every launch think it was out of date.
      const build = parts[2].slice(0, -4);
      const row = { app, build, size: b.size, url: b.url, uploadedAt: b.uploadedAt };
      if (!latest[app] || new Date(row.uploadedAt) > new Date(latest[app].uploadedAt))
        latest[app] = row;
    }

    if (want) {
      let row = latest[want];
      // Builds moved to GitHub Releases; this store no longer holds them.
      // Rather than 404 at consoles still pointed here -- which is every
      // console until someone walks a USB cable over -- forward what GitHub
      // says. An updater that cannot reach a manifest never updates again,
      // so the old address has to keep answering for as long as anything
      // still asks it.
      if (!row) row = await fromGithub(want);
      if (!row) return res.status(404).json({ error: `no builds for ${want}` });
      // Deliberately flat and tiny: the console parses this with strstr, not
      // a JSON library, so every field it needs is one unambiguous line.
      if (req.query.format === 'text') {
        res.setHeader('Content-Type', 'text/plain; charset=utf-8');
        return res.status(200).send(
          `build=${row.build}\nsize=${row.size}\nurl=${row.url}\n`
        );
      }
      return res.status(200).json(row);
    }
    return res.status(200).json({ apps: Object.values(latest) });
  } catch (err) {
    return res.status(500).json({ error: String((err && err.message) || err) });
  }
}
