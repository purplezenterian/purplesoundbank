// Core: helpers, sign-in state, the one-device rules, navigation and routing.
(function () {
  const C = window.PSB_CONFIG;
  const api = C.mode === 'live' ? window.PSB_LIVE : window.PSB_MOCK;
  const A = (window.PSB_APP = { api, C, state: { session: null, profile: null, sub: null, counts: { unread: 0, requests: 0 }, blocker: null }, routes: {}, timers: [] });
  const S = A.state;

  /* ---------- tiny helpers ---------- */
  const h = (tag, props, ...kids) => {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(props || {})) {
      if (v === false || v === null || v === undefined) continue;
      if (k === 'class') el.className = v;
      else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
      else if (k === 'value') el.value = v;
      else if (k === 'html') throw new Error('no html');
      else el.setAttribute(k, v === true ? '' : v);
    }
    const add = x => { if (x === null || x === undefined || x === false) return; if (Array.isArray(x)) x.forEach(add); else el.appendChild(x.nodeType ? x : document.createTextNode(String(x))); };
    kids.forEach(add);
    return el;
  };
  A.h = h;
  const $ = s => document.querySelector(s);
  A.$ = $;
  A.toast = msg => { const t = $('#toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(A._tt); A._tt = setTimeout(() => t.classList.remove('show'), 3200); };
  A.fail = e => { console.error(e); if (e && e.code === 'other_device') return kicked(); if (e && e.code === 'no_subscription') { refreshState().then(route); return; } A.toast((e && e.message) || 'Something went wrong'); };
  A.modal = node => { const m = $('#modal'); m.replaceChildren(h('div', { class: 'card', role: 'dialog', 'aria-modal': 'true' }, node)); m.hidden = false; m.onclick = ev => { if (ev.target === m) A.closeModal(); }; const f = m.querySelector('input,textarea,button'); if (f) f.focus(); };
  A.closeModal = () => { const m = $('#modal'); m.hidden = true; m.replaceChildren(); };
  document.addEventListener('keydown', e => { if (e.key === 'Escape') A.closeModal(); });
  A.ago = iso => { const s = Math.max(1, (Date.now() - Date.parse(iso)) / 1000); if (s < 60) return 'just now'; if (s < 3600) return Math.floor(s / 60) + 'm ago'; if (s < 86400) return Math.floor(s / 3600) + 'h ago'; if (s < 86400 * 30) return Math.floor(s / 86400) + 'd ago'; return new Date(iso).toLocaleDateString(undefined, { day: 'numeric', month: 'short', year: 'numeric' }); };
  A.date = iso => new Date(iso).toLocaleDateString(undefined, { day: 'numeric', month: 'long', year: 'numeric' });
  A.avatar = (u, cls) => h('span', { class: 'av ' + (cls || ''), style: 'background:' + ((u && u.avatar_color) || '#7b2fff'), 'aria-hidden': 'true' }, ((u && (u.display_name || u.username)) || '?').trim().charAt(0));
  A.who = u => h('a', { class: 'who', href: '#/u/' + u.username }, A.avatar(u, 'sm'), u.display_name || u.username);
  A.go = hash => { if (location.hash === hash) route(); else location.hash = hash; };
  A.isMember = () => !!(S.sub && ['trialing', 'active'].includes(S.sub.status) && !S.blocker);
  A.setTitle = t => { document.title = t ? t + ' · ' + C.siteName : C.siteName; };
  A.every = (fn, ms) => { const id = setInterval(fn, ms); A.timers.push(id); return id; };
  A.render = (...nodes) => { const v = $('#view'); v.replaceChildren(...nodes.flat().filter(Boolean)); window.scrollTo(0, 0); };
  const form = (fields, submitLabel, onSubmit, extra) => {
    const msg = h('div', { 'aria-live': 'polite' });
    const f = h('form', { novalidate: true, onsubmit: async ev => { ev.preventDefault(); msg.replaceChildren(); const vals = {}; fields.forEach(x => { vals[x.name] = f.elements[x.name].value; }); const btn = f.querySelector('button[type=submit]'); btn.disabled = true; try { await onSubmit(vals, msg); } catch (e) { msg.replaceChildren(h('div', { class: 'msg err' }, e.message || 'Something went wrong')); if (e.code === 'other_device') A.fail(e); } btn.disabled = false; } },
      fields.map(x => h('div', { class: 'field' }, h('label', { for: 'f_' + x.name }, x.label),
        x.type === 'textarea' ? h('textarea', { id: 'f_' + x.name, name: x.name, maxlength: x.max || 5000, placeholder: x.ph || '' }, x.value || '') : x.type === 'select' ? h('select', { id: 'f_' + x.name, name: x.name }, x.options.map(o => h('option', { value: o[0], selected: o[0] === x.value }, o[1]))) :
          h('input', { id: 'f_' + x.name, name: x.name, type: x.type || 'text', autocomplete: x.auto || 'off', placeholder: x.ph || '', maxlength: x.max || 200, value: x.value || '' }))),
      msg, extra || null, h('button', { class: 'btn primary', type: 'submit', style: 'width:100%' }, submitLabel));
    return f;
  };
  A.form = form;

  /* ---------- state: session, device rules, membership ---------- */
  async function refreshState() {
    const s = await api.getSession();
    S.session = s; S.blocker = null;
    if (!s) { S.profile = null; S.sub = null; S.counts = { unread: 0, requests: 0 }; return; }
    const r = await api.registerDevice(false);
    if (r.status === 'busy') S.blocker = { type: 'busy', label: r.label };
    else if (r.status === 'device_taken') S.blocker = { type: 'taken' };
    else if (r.status === 'too_many_devices') S.blocker = { type: 'toomany' };
    S.sub = await api.getSubscription();
    S.profile = await api.me();
    if (!S.blocker) await refreshCounts();
  }
  async function refreshCounts() { if (!A.isMember()) return; try { S.counts = await api.counts(); renderNav(); } catch (e) { /* ignore */ } }
  A.refreshState = refreshState;

  async function kicked() {
    if (S.blocker && S.blocker.type === 'kicked') return;
    S.blocker = { type: 'kicked' };
    try { await api.signOut(); } catch (e) {}
    S.session = null; S.profile = null; S.sub = null;
    route();
  }
  async function heartbeat() {
    if (!S.session || S.blocker) return;
    try { const r = await api.heartbeat(); if (r.kicked) kicked(); } catch (e) { /* network blip */ }
  }

  /* ---------- navigation ---------- */
  function renderNav() {
    document.body.classList.toggle('playing', location.hash.startsWith('#/play'));
    const here = (location.hash.replace(/^#\/?/, '').split('/')[0] || '');
    const link = (href, text, key, badge) => h('a', { href, class: here === key ? 'on' : '' }, text, badge ? h('span', { class: 'badge' }, badge) : null);
    let links = [], right = [];
    if (S.session && !S.blocker && A.isMember()) {
      links = [link('#/play', 'Play', 'play'), link('#/hub', 'Hub', 'hub'), link('#/friends', 'Friends', 'friends', S.counts.requests || ''), link('#/messages', 'Messages', 'messages', S.counts.unread || ''),
        S.profile && S.profile.is_admin ? link('#/admin', 'Admin', 'admin') : null, link('#/account', 'Account', 'account')];
    } else if (S.session) {
      links = [link('#/pricing', 'Membership', 'pricing'), link('#/account', 'Account', 'account')];
      right = [h('button', { class: 'btn small', onclick: signOut }, 'Sign out')];
    } else {
      links = [link('#/pricing', 'Pricing', 'pricing')];
      right = [h('a', { class: 'btn small', href: '#/login' }, 'Log in'), h('a', { class: 'btn small primary', href: '#/signup' }, 'Start free month')];
    }
    $('#nav').replaceChildren(h('div', { class: 'nav-in' }, h('a', { class: 'brand', href: S.session ? (A.isMember() ? '#/hub' : '#/pricing') : '#/' }, h('span', { class: 'mark' }, h('i'), h('i'), h('i')), h('span', null, C.siteName)), h('nav', { class: 'nav-links', 'aria-label': 'Main' }, links), h('div', { class: 'nav-right' }, right)));
    $('#foot').replaceChildren(h('div', null, h('a', { href: '#/terms' }, 'Terms'), h('a', { href: '#/privacy' }, 'Privacy'), h('a', { href: '#/rules' }, 'Community rules')), h('p', { class: 'small' }, '© ' + new Date().getFullYear() + ' ' + C.siteName + '. Made for the church musician.'));
    $('#nav').style.display = document.body.classList.contains('playing') ? 'none' : '';
    $('#foot').style.display = document.body.classList.contains('playing') ? 'none' : '';
  }
  async function signOut() { try { await api.signOut(); } catch (e) {} S.session = S.profile = S.sub = null; S.blocker = null; A.go('#/'); renderNav(); }
  A.signOut = signOut;

  function demoBar() {
    const bar = $('#demo-bar'); if (C.mode !== 'mock') { bar.hidden = true; return; }
    const d = api.demo; bar.hidden = false;
    const btn = (t, fn) => h('button', { type: 'button', onclick: fn }, t);
    bar.replaceChildren(h('b', null, 'Demo mode'), h('span', { class: 'muted' }, 'Fake data in this browser. No real accounts or payments.'),
      btn(d.isPretending() ? 'Back to this device' : 'Pretend this is another device', () => { d.isPretending() ? d.backToThisDevice() : d.pretendOtherDevice(); location.reload(); }),
      btn('Sign in on another device', async () => { d.loginElsewhere(); A.toast('Another device just signed in to your account.'); heartbeat(); }),
      btn('Make me admin', async () => { d.makeAdmin(); await refreshState(); route(); A.toast('You are an admin now.'); }),
      btn('Reset demo', () => { d.reset(); location.hash = '#/'; location.reload(); }));
  }

  /* ---------- blocker screens (the one-device rules) ---------- */
  function blockerScreen() {
    const b = S.blocker;
    if (b.type === 'busy') return A.render(h('div', { class: 'card narrow center' }, h('h2', null, 'Already in use'), h('p', { class: 'muted' }, 'Your account is open on ' + (b.label || 'another device') + '. An account can only be used on one device at a time.'),
      h('div', { class: 'row', style: 'justify-content:center' }, h('button', { class: 'btn primary', onclick: async () => { try { await api.registerDevice(true); await refreshState(); A.go(A.isMember() ? '#/hub' : '#/pricing'); renderNav(); } catch (e) { A.fail(e); } } }, 'Use it here instead'), h('button', { class: 'btn', onclick: signOut }, 'Sign out')), h('p', { class: 'small muted', style: 'margin-top:14px' }, 'If you continue, the other device is signed out straight away.')));
    if (b.type === 'taken') return A.render(h('div', { class: 'card narrow center' }, h('h2', null, 'This device belongs to another account'), h('p', { class: 'muted' }, 'Each device can only be used with one account, so this one cannot sign in as you. Sign in on a device that is linked to your account, or use the device you signed up on.'), h('button', { class: 'btn', onclick: signOut }, 'Sign out')));
    if (b.type === 'toomany') return A.render(h('div', { class: 'card narrow' }, h('h2', null, 'Your account is on 3 devices already'), h('p', { class: 'muted' }, 'Each account can be linked to up to 3 devices. If you changed phone or computer, ask for a reset. We approve these by hand to keep accounts from being shared.'), resetForm(), h('hr'), h('button', { class: 'btn', onclick: signOut }, 'Sign out')));
    if (b.type === 'kicked') return A.render(h('div', { class: 'card narrow center' }, h('h2', null, 'You were signed out'), h('p', { class: 'muted' }, 'Your account was opened on another device. An account can only be used on one device at a time.'), h('p', { class: 'small muted' }, 'If that was not you, change your password after you sign in.'), h('button', { class: 'btn primary', onclick: () => { S.blocker = null; A.go('#/login'); } }, 'Sign in again')));
  }
  function resetForm() {
    const box = h('div', null);
    const show = async () => { let pending = false; try { pending = await api.myResetPending(); } catch (e) {} box.replaceChildren(pending ? h('div', { class: 'msg ok' }, 'Your request is waiting for approval. You will be able to sign in on your new device once it is approved.') : form([{ name: 'note', label: 'What changed? (optional)', type: 'textarea', max: 300, ph: 'e.g. I got a new laptop' }], 'Ask for a device reset', async v => { await api.requestReset(v.note); A.toast('Request sent'); show(); })); };
    show(); return box;
  }
  A.resetForm = resetForm;

  /* ---------- router ---------- */
  const PUBLIC = new Set(['', 'pricing', 'login', 'signup', 'forgot', 'terms', 'privacy', 'rules']);
  const NEEDS_LOGIN = new Set(['account', 'admin']);
  async function route() {
    A.timers.forEach(clearInterval); A.timers.length = 0;
    const raw = location.hash.replace(/^#/, '') || '/';
    const [path, qs] = raw.split('?'); A.query = new URLSearchParams(qs || '');
    const seg = path.split('/').filter(Boolean); const top = seg[0] || '';
    A.seg = seg; renderNav();
    if (S.blocker && !['terms', 'privacy', 'rules'].includes(top)) return blockerScreen();
    if (!PUBLIC.has(top) && !S.session) { A.next = raw; return A.go('#/login'); }
    if (S.session && (top === 'login' || top === 'signup')) return A.go(A.isMember() ? '#/hub' : '#/pricing');
    if (!PUBLIC.has(top) && !NEEDS_LOGIN.has(top) && !A.isMember()) return A.go('#/pricing');
    if (top === 'admin' && !(S.profile && S.profile.is_admin)) return A.go('#/');
    const fn = A.routes[top];
    if (!fn) return A.render(h('div', { class: 'empty' }, h('h2', null, 'Page not found'), h('a', { class: 'btn', href: '#/' }, 'Go home')));
    try { await fn(seg.slice(1)); } catch (e) { A.fail(e); }
    renderNav();
  }
  A.route = route;

  async function boot() {
    $('#view').replaceChildren(h('div', { class: 'empty' }, 'Loading…'));
    try {
      if (C.mode === 'live' && (!C.supabaseUrl || !C.supabaseAnonKey)) throw new Error('Live mode needs the Supabase URL and key in js/config.js');
      await api.init();
      api.onRecovery(() => A.routes._recovery());
      const fromEmail = /access_token=|type=signup|type=recovery|error_description=/.test(location.hash);
      if (fromEmail) { await new Promise(r => setTimeout(r, 400)); history.replaceState(null, '', location.pathname + '#/pricing'); }
      await refreshState();
    } catch (e) { A.render(h('div', { class: 'card narrow' }, h('h2', null, 'Could not start'), h('p', { class: 'msg err' }, e.message))); return; }
    demoBar(); window.addEventListener('hashchange', route); route();
    setInterval(heartbeat, 30000); setInterval(refreshCounts, 15000);
    document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible') heartbeat(); });
  }
  A.boot = boot;
  A.afterAuth = async () => { await refreshState(); const next = A.next; A.next = null; demoBar(); A.go(next && next !== '/login' && next !== '/signup' ? '#' + next : (A.isMember() ? '#/hub' : '#/pricing')); renderNav(); };
  A.kicked = kicked;
})();
