// Keeps the site shell on the device so Ambassadors can open it with no signal. Network first, saved copy as fallback.
const CACHE = 'psb-shell-v1';
const EXTRA = ['https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.45.4/dist/umd/supabase.js'];
self.addEventListener('install', e => {
  e.waitUntil((async () => {
    const c = await caches.open(CACHE);
    try {
      const html = await (await fetch('index.html', { cache: 'reload' })).text();
      const urls = ['index.html', './'];
      html.replace(/(?:src|href)="([^"]+\.(?:js|css)[^"]*)"/g, (m, u) => { if (!/^https?:/.test(u)) urls.push(u); });
      await Promise.all(urls.concat(EXTRA).map(u => c.add(new Request(u, { cache: 'reload' })).catch(() => {})));
    } catch (x) {}
    self.skipWaiting();
  })());
});
self.addEventListener('activate', e => e.waitUntil((async () => { for (const k of await caches.keys()) if (k !== CACHE) await caches.delete(k); await self.clients.claim(); })()));
self.addEventListener('fetch', e => {
  const r = e.request, u = new URL(r.url);
  if (r.method !== 'GET' || r.headers.has('range')) return;
  const same = u.origin === location.origin && !/\/videos\//.test(u.pathname);
  const cdn = /(^|\.)(jsdelivr\.net|fonts\.googleapis\.com|fonts\.gstatic\.com)$/.test(u.hostname);
  if (!same && !cdn) return;                       // Supabase, Stripe and the private instrument file always go to the network
  e.respondWith((async () => {
    const c = await caches.open(CACHE);
    try { const res = await fetch(r); if (res && (res.ok || res.type === 'opaque')) c.put(r, res.clone()).catch(() => {}); return res; }
    catch (err) {
      const hit = await c.match(r, { ignoreSearch: false }) || await c.match(r, { ignoreSearch: true });
      if (hit) return hit;
      if (r.mode === 'navigate') { const idx = await c.match('index.html', { ignoreSearch: true }) || await c.match('./'); if (idx) return idx; }
      throw err;
    }
  })());
});
