// Works out which device this is. A random id is kept in three places so clearing one of them does not make the device look new,
// plus a fingerprint of the hardware/browser that the server uses to recognise the same device again.
(function () {
  const KEY = 'psb.did';
  const rnd = () => (crypto.randomUUID ? crypto.randomUUID() : 'd-' + Math.random().toString(16).slice(2) + Date.now().toString(16));
  const okId = v => typeof v === 'string' && /^[A-Za-z0-9-]{16,80}$/.test(v);

  function idbGet() {
    return new Promise(res => {
      try {
        const rq = indexedDB.open('psb-device', 1);
        rq.onupgradeneeded = () => rq.result.createObjectStore('k');
        rq.onerror = () => res(null);
        rq.onsuccess = () => { try { const g = rq.result.transaction('k').objectStore('k').get(KEY); g.onsuccess = () => res(g.result || null); g.onerror = () => res(null); } catch (e) { res(null); } };
      } catch (e) { res(null); }
    });
  }
  function idbSet(v) {
    try {
      const rq = indexedDB.open('psb-device', 1);
      rq.onupgradeneeded = () => rq.result.createObjectStore('k');
      rq.onsuccess = () => { try { rq.result.transaction('k', 'readwrite').objectStore('k').put(v, KEY); } catch (e) {} };
    } catch (e) {}
  }
  const cookieGet = () => { const m = document.cookie.match(/(?:^|; )psb_did=([^;]+)/); return m ? decodeURIComponent(m[1]) : null; };
  const cookieSet = v => { try { document.cookie = 'psb_did=' + encodeURIComponent(v) + '; max-age=' + 60 * 60 * 24 * 730 + '; path=/; SameSite=Lax' + (location.protocol === 'https:' ? '; Secure' : ''); } catch (e) {} };

  let cached = null;
  async function deviceId() {
    if (cached) return cached;
    const override = sessionStorage.getItem('psb.demo.device');           // demo mode only: "pretend this is a different device"
    if (override) return (cached = override);
    let id = null;
    try { id = localStorage.getItem(KEY); } catch (e) {}
    if (!okId(id)) id = cookieGet();
    if (!okId(id)) id = await idbGet();
    if (!okId(id)) id = rnd();
    try { localStorage.setItem(KEY, id); } catch (e) {}
    cookieSet(id); idbSet(id);
    return (cached = id);
  }

  function fingerprint() {
    const parts = [navigator.platform, navigator.hardwareConcurrency, navigator.deviceMemory, screen.width + 'x' + screen.height + 'x' + screen.colorDepth,
      window.devicePixelRatio, Intl.DateTimeFormat().resolvedOptions().timeZone, (navigator.languages || [navigator.language]).join(',')];
    try {
      const c = document.createElement('canvas'); const gl = c.getContext('webgl');
      const ext = gl && gl.getExtension('WEBGL_debug_renderer_info');
      if (ext) parts.push(gl.getParameter(ext.UNMASKED_VENDOR_WEBGL), gl.getParameter(ext.UNMASKED_RENDERER_WEBGL));
      const c2 = document.createElement('canvas'); c2.width = 220; c2.height = 40; const x = c2.getContext('2d');
      x.textBaseline = 'top'; x.font = '16px Arial'; x.fillStyle = '#f60'; x.fillRect(10, 5, 80, 24); x.fillStyle = '#069'; x.fillText('Purple SoundBank ♫', 4, 8);
      parts.push(c2.toDataURL().slice(-60));
    } catch (e) {}
    return parts.join('|');
  }

  function label() {
    const ua = navigator.userAgent;
    const b = /Edg\//.test(ua) ? 'Edge' : /OPR\//.test(ua) ? 'Opera' : /Chrome\//.test(ua) ? 'Chrome' : /Firefox\//.test(ua) ? 'Firefox' : /Safari\//.test(ua) ? 'Safari' : 'Browser';
    const o = /iPhone/.test(ua) ? 'iPhone' : /iPad/.test(ua) ? 'iPad' : /Android/.test(ua) ? 'Android' : /Windows/.test(ua) ? 'Windows' : /Mac OS X/.test(ua) ? 'Mac' : /Linux/.test(ua) ? 'Linux' : 'device';
    return b + ' on ' + o;
  }

  window.PSB_DEVICE = { deviceId, fingerprint, label };
})();
