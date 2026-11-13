// Offline mode for Ambassadors: the instrument is saved on the device the first time it is opened online.
// A 14 day pass is renewed every time it is opened online (that load already proves the membership is paid).
(function () {
  const A = window.PSB_APP, h = A.h, C = window.PSB_CONFIG;
  const DAYS = 14, PASS = 'site.offline.pass';
  const idb = () => new Promise((res, rej) => { const r = indexedDB.open('psb-offline', 1); r.onupgradeneeded = () => r.result.createObjectStore('kv'); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); });
  const put = async (k, v) => { const d = await idb(); return new Promise((res, rej) => { const t = d.transaction('kv', 'readwrite'); t.objectStore('kv').put(v, k); t.oncomplete = res; t.onerror = () => rej(t.error); }); };
  const get = async k => { const d = await idb(); return new Promise((res, rej) => { const q = d.transaction('kv').objectStore('kv').get(k); q.onsuccess = () => res(q.result); q.onerror = () => rej(q.error); }); };
  const pass = () => { try { const p = JSON.parse(localStorage.getItem(PASS) || 'null'); return p && p.until > Date.now() ? p : null; } catch (e) { return null; } };
  const O = A.offline = {
    eligible: p => !!(p && (p.is_admin || A.isAmb(p))),
    pass,
    async save(html, p) { await put('instrument', html); try { localStorage.setItem(PASS, JSON.stringify({ uid: p.id, name: p.username, until: Date.now() + DAYS * 864e5 })); } catch (e) {} },
    async load() { try { return await get('instrument'); } catch (e) { return null; } },
    async saveSound(id, samples) { try { await put('sound:' + id, samples); } catch (e) {} },
    async sound(id) { try { return await get('sound:' + id); } catch (e) { return null; } },
    home() {
      const p = pass(); document.body.classList.remove('playing');
      const nav = document.getElementById('nav'); if (nav) nav.replaceChildren(h('div', { class: 'row', style: 'padding:14px 20px' }, h('b', null, C.siteName)));
      A.render(h('div', { class: 'card narrow center' }, h('h2', null, 'You are offline'),
        h('p', { class: 'muted' }, p ? 'Ambassador offline mode is on. Your pass is good until ' + new Date(p.until).toLocaleDateString('en-GB', { day: 'numeric', month: 'long', year: 'numeric' }) + '.' : 'No offline pass on this device.'),
        p ? h('button', { class: 'btn primary', onclick: () => { A.offlinePlay = true; A.query = new URLSearchParams(); A.routes.play(); } }, 'Open the instrument') : null,
        h('p', { class: 'small muted' }, 'Connect to the internet at least once every ' + DAYS + ' days to keep your pass.'),
        h('button', { class: 'btn small', onclick: () => location.reload() }, 'Try again')));
      window.addEventListener('online', () => location.reload(), { once: true });
    },
  };
  if ('serviceWorker' in navigator && (location.protocol === 'https:' || location.hostname === 'localhost')) navigator.serviceWorker.register('sw.js').catch(() => {});
})();
