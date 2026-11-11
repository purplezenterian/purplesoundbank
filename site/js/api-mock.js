// DEMO backend: everything lives in this browser (localStorage). It follows the same rules as the real database and functions,
// so you can click through sign-up, the free month, the one-device rules and the hub before connecting anything real.
(function () {
  const KEY = 'psb.mock.v1';
  const DAY = 86400000;
  const now = () => Date.now();
  const iso = t => new Date(t).toISOString();
  const uid = () => 'u' + Math.random().toString(16).slice(2, 10);
  let S = load();

  function load() { try { const s = JSON.parse(localStorage.getItem(KEY)); if (s && s.v === 1) return s; } catch (e) {} return seed(); }
  function save() { try { localStorage.setItem(KEY, JSON.stringify(S)); } catch (e) {} }

  function seed() {
    const colors = ['#7b2fff', '#e0489f', '#2fa8ff', '#ffb347', '#3ecf8e', '#ff6b6b'];
    const mk = (username, display, bio, i) => ({ id: 'seed_' + username, username, display_name: display, bio, avatar_color: colors[i % colors.length], created_at: iso(now() - (30 - i) * DAY), seed: true });
    const users = [
      mk('grace_keys', 'Grace Keys', 'Sunday morning organist. Pads and strings are my love language.', 0),
      mk('minister_kay', 'Minister Kay', 'Choir director, 12 years. Always hunting for a warmer pad.', 1),
      mk('pastor_dre', 'Pastor Dre', 'Plays by ear. Ask me anything about worship flow.', 2),
      mk('keys_by_tay', 'Tay', 'Youth band keys. Learning set lists the hard way.', 3),
    ];
    const t = h => iso(now() - h * 3600000);
    const posts = [
      { id: 1, author_id: 'seed_grace_keys', category: 'general', title: 'Welcome to the hub!', body: 'Say hi, share what you play, and ask anything. Be kind and keep it about the music and the ministry.', pinned: true, deleted: false, comment_count: 0, like_count: 3, created_at: t(90) },
      { id: 2, author_id: 'seed_minister_kay', category: 'questions', title: 'How do you set up a smooth set list for Sunday?', body: 'I want to move between songs without any gap in the sound. Anyone using the Perform tab live? What does your order look like?', pinned: false, deleted: false, comment_count: 0, like_count: 5, created_at: t(30) },
      { id: 3, author_id: 'seed_pastor_dre', category: 'patches', title: 'Pad + strings layer that fills the whole room', body: 'Poly Pad with Multi Strings, strings fader at about 60%, pad at 100%. Turn the mod wheel slowly during the bridge.', pinned: false, deleted: false, comment_count: 0, like_count: 8, created_at: t(20) },
      { id: 4, author_id: 'seed_keys_by_tay', category: 'showtell', title: 'First time using this at youth service', body: 'Worked perfectly through the whole set. The 80s brass on the offering song was a hit!', pinned: false, deleted: false, comment_count: 0, like_count: 2, created_at: t(8) },
      { id: 5, author_id: 'seed_grace_keys', category: 'gospel', title: 'Tarry songs: what are you playing underneath?', body: 'I keep it simple with Poly Pad and a slow filter sweep. What do you do when the spirit moves?', pinned: false, deleted: false, comment_count: 0, like_count: 4, created_at: t(3) },
    ];
    const comments = [
      { id: 1, post_id: 2, author_id: 'seed_grace_keys', parent_id: null, body: 'I put the whole set in order, then use the arrow keys on a laptop to move down the list. Never miss a beat.', deleted: false, created_at: t(26) },
      { id: 2, post_id: 2, author_id: 'seed_pastor_dre', parent_id: 1, body: 'Same here. The old song keeps ringing while the next one loads, which is great for transitions.', deleted: false, created_at: t(24) },
      { id: 3, post_id: 3, author_id: 'seed_minister_kay', parent_id: null, body: 'Trying this tonight at rehearsal, thank you!', deleted: false, created_at: t(18) },
    ];
    posts.forEach(p => { p.comment_count = comments.filter(c => c.post_id === p.id).length; });
    return { v: 1, users, accounts: [], current: null, subs: {}, devices: [], active: {}, posts, comments, likes: [], friendships: [], messages: [], blocks: [], reports: [], resets: [], next: { post: 6, comment: 4, fr: 1, msg: 1, report: 1, reset: 1 } };
  }

  const err = (m, code) => { const e = new Error(m); e.code = code || 'error'; return e; };
  const me = () => (S.current ? S.users.find(u => u.id === S.current.userId) : null);
  const acct = id => S.accounts.find(a => a.id === id);
  const pub = u => u && ({ id: u.id, username: u.username, display_name: u.display_name || u.username, bio: u.bio || '', avatar_color: u.avatar_color, created_at: u.created_at, is_admin: !!u.is_admin });
  const byId = id => S.users.find(u => u.id === id);
  const isMember = () => {
    if (!S.current) return false;
    const sub = S.subs[S.current.userId];
    const a = S.active[S.current.userId];
    return !!(sub && ['trialing', 'active'].includes(sub.status) && a && a.sessionId === S.current.sessionId);
  };
  const needMember = () => { if (!S.current) throw err('Please sign in', 'auth'); const a = S.active[S.current.userId]; if (!a || a.sessionId !== S.current.sessionId) throw err('other_device', 'other_device'); if (!isMember()) throw err('no_subscription', 'no_subscription'); };
  const delay = (v) => new Promise(r => setTimeout(() => r(v), 60));
  const clone = x => JSON.parse(JSON.stringify(x));

  // Seeded friends accept requests after a moment and reply to messages, so you can try the whole flow alone.
  function botTick() {
    const m = me(); if (!m) return;
    let changed = false;
    for (const f of S.friendships) {
      if (f.status === 'pending' && f.requester_id === m.id && f.botAt && now() >= f.botAt) { f.status = 'accepted'; changed = true; }
    }
    for (const msg of S.messages) {
      if (msg.botReplyAt && now() >= msg.botReplyAt && !msg.botDone) {
        msg.botDone = true; changed = true;
        const from = byId(msg.recipient_id);
        if (from && from.seed) S.messages.push({ id: S.next.msg++, sender_id: from.id, recipient_id: msg.sender_id, body: ['Hey! Welcome to the hub \u{1F44B}', 'Love that. Which sound are you using most?', 'Amen! Let me know how Sunday goes.'][msg.id % 3], read_at: null, created_at: iso(now()) });
      }
    }
    if (changed) save();
  }

  const api = {
    mode: 'mock',
    async init() { S = load(); },
    async getSession() { botTick(); const u = me(); return u ? { user: { id: u.id, email: acct(u.id).email }, sessionId: S.current.sessionId } : null; },
    async signUp({ email, password, username }) {
      email = String(email || '').trim().toLowerCase(); username = String(username || '').trim().toLowerCase();
      if (!/^[a-z0-9_]{3,20}$/.test(username)) throw err('Usernames are 3-20 letters, numbers or underscores');
      if (!/^\S+@\S+\.\S+$/.test(email)) throw err('Please enter a valid email address');
      if ((password || '').length < 6) throw err('Password must be at least 6 characters');
      if (S.accounts.some(a => a.email === email)) throw err('An account with that email already exists');
      if (S.users.some(u => u.username === username)) throw err('That username is taken');
      const id = uid();
      S.accounts.push({ id, email, password });
      S.users.push({ id, username, display_name: username, bio: '', avatar_color: '#7b2fff', is_admin: S.accounts.length === 1 && false, trial_used: false, created_at: iso(now()) });
      S.current = { userId: id, sessionId: uid() + uid() }; save();
      return delay({ needsConfirm: false });
    },
    async signIn(email, password) {
      const a = S.accounts.find(x => x.email === String(email || '').trim().toLowerCase() && x.password === password);
      if (!a) throw err('Wrong email or password');
      S.current = { userId: a.id, sessionId: uid() + uid() }; save();
      return delay(true);
    },
    async signOut() { S.current = null; save(); },
    async resetPassword() { return delay(true); },
    onRecovery() {},
    async updatePassword() { return true; },

    // ---- devices and sessions
    async registerDevice(takeover) {
      const m = me(); if (!m) throw err('Please sign in', 'auth');
      const deviceId = await PSB_DEVICE.deviceId(); const fp = PSB_DEVICE.fingerprint(); const label = PSB_DEVICE.label() + (sessionStorage.getItem('psb.demo.device') ? ' (pretend)' : '');
      const dev = S.devices.find(d => d.deviceId === deviceId);
      if (dev && dev.userId !== m.id) return delay({ status: 'device_taken' });
      const mine = S.devices.filter(d => d.userId === m.id);
      if (!dev) {
        const same = mine.find(d => d.fp === fp && !sessionStorage.getItem('psb.demo.device'));
        if (same) same.deviceId = deviceId;
        else { if (mine.length >= 3) return delay({ status: 'too_many_devices' }); S.devices.push({ userId: m.id, deviceId, fp, label, last_seen: iso(now()) }); }
      }
      const act = S.active[m.id];
      const busy = act && act.sessionId !== S.current.sessionId && now() - act.last_seen < 120000;
      if (busy && !takeover) { save(); return delay({ status: 'busy', label: act.label }); }
      S.active[m.id] = { sessionId: S.current.sessionId, deviceId, label, last_seen: now() }; save();
      return delay({ status: 'ok' });
    },
    async heartbeat() {
      botTick();
      if (!S.current) return { kicked: true };
      const a = S.active[S.current.userId];
      if (!a || a.sessionId !== S.current.sessionId) return { kicked: true };
      a.last_seen = now(); save(); return { ok: true };
    },
    async myDevices() { const m = me(); const id = await PSB_DEVICE.deviceId(); return S.devices.filter(d => d.userId === m.id).map(d => ({ label: d.label, last_seen: d.last_seen, current: d.deviceId === id })); },
    async requestReset(note) { const m = me(); if (S.resets.some(r => r.user_id === m.id && r.status === 'pending')) throw err('You already have a request waiting.'); S.resets.push({ id: S.next.reset++, user_id: m.id, note: String(note || '').slice(0, 300), status: 'pending', created_at: iso(now()) }); save(); return true; },
    async myResetPending() { const m = me(); return S.resets.some(r => r.user_id === m.id && r.status === 'pending'); },

    // ---- billing
    async getSubscription() { const m = me(); if (!m) return null; const s = S.subs[m.id]; return s ? clone(s) : null; },
    async startCheckout() { const m = me(); return delay({ mock: true, trial: !m.trial_used }); },
    async mockCompleteCheckout() {
      const m = me(); const trial = !m.trial_used;
      S.subs[m.id] = { status: trial ? 'trialing' : 'active', trial_end: trial ? iso(now() + 30 * DAY) : null, current_period_end: iso(now() + 30 * DAY), cancel_at_period_end: false };
      m.trial_used = true; save(); return true;
    },
    async openPortal() { return delay({ mock: true }); },
    async mockPortalAction(a) { const m = me(); const s = S.subs[m.id]; if (!s) return; if (a === 'cancel') s.cancel_at_period_end = true; if (a === 'resume') s.cancel_at_period_end = false; if (a === 'end') s.status = 'canceled'; if (a === 'pastdue') s.status = 'past_due'; if (a === 'restore') s.status = s.trial_end && now() < Date.parse(s.trial_end) ? 'trialing' : 'active'; save(); },
    async instrumentUrl() { needMember(); return PSB_CONFIG.mockInstrumentUrl; },

    // ---- profiles
    async me() { const u = me(); return u ? Object.assign(pub(u), { is_admin: !!u.is_admin, trial_used: !!u.trial_used }) : null; },
    async getProfile(username) { needMember(); const u = S.users.find(x => x.username === String(username).toLowerCase()); return u ? pub(u) : null; },
    async updateProfile(p) { const u = me(); if (p.display_name !== undefined) u.display_name = String(p.display_name).slice(0, 40); if (p.bio !== undefined) u.bio = String(p.bio).slice(0, 300); if (p.avatar_color) u.avatar_color = p.avatar_color; save(); return pub(u); },

    // ---- hub
    _post(p, liked) { const a = byId(p.author_id); return Object.assign({}, p, { author: pub(a), liked: !!liked }); },
    async listPosts({ category, before } = {}) {
      needMember(); const m = me();
      let list = S.posts.filter(p => !p.deleted && (!category || p.category === category));
      const likedSet = new Set(S.likes.filter(l => l.user_id === m.id).map(l => l.post_id));
      const pinned = before ? [] : list.filter(p => p.pinned).map(p => api._post(p, likedSet.has(p.id)));
      list = list.filter(p => !p.pinned).sort((a, b) => b.id - a.id);
      if (before) list = list.filter(p => p.id < before);
      const page = list.slice(0, 20);
      return delay({ pinned, posts: page.map(p => api._post(p, likedSet.has(p.id))), more: list.length > 20 });
    },
    async getPost(id) { needMember(); const p = S.posts.find(x => x.id === Number(id) && !x.deleted); if (!p) return null; return api._post(p, S.likes.some(l => l.user_id === me().id && l.post_id === p.id)); },
    async createPost({ category, title, body }) {
      needMember(); const m = me();
      title = String(title || '').trim(); body = String(body || '').trim();
      if (title.length < 3) throw err('Give your post a title (at least 3 letters)'); if (!body) throw err('Write something in your post');
      if (S.posts.filter(p => p.author_id === m.id && now() - Date.parse(p.created_at) < 3600000).length >= 15) throw err('Slow down a little - you have hit the hourly limit for this.');
      const p = { id: S.next.post++, author_id: m.id, category, title: title.slice(0, 120), body: body.slice(0, 5000), pinned: false, deleted: false, comment_count: 0, like_count: 0, created_at: iso(now()) };
      S.posts.push(p); save(); return api._post(p, false);
    },
    async deletePost(id) { needMember(); const m = me(); const p = S.posts.find(x => x.id === id); if (p && (p.author_id === m.id || m.is_admin)) { p.deleted = true; save(); } },
    async pinPost(id, v) { needMember(); if (!me().is_admin) throw err('Not allowed'); const p = S.posts.find(x => x.id === id); if (p) { p.pinned = v; save(); } },
    async listComments(postId) { needMember(); return delay(S.comments.filter(c => c.post_id === Number(postId) && !c.deleted).map(c => Object.assign({}, c, { author: pub(byId(c.author_id)) }))); },
    async addComment(postId, body, parentId) {
      needMember(); const m = me(); body = String(body || '').trim(); if (!body) throw err('Write a comment first');
      const c = { id: S.next.comment++, post_id: Number(postId), author_id: m.id, parent_id: parentId || null, body: body.slice(0, 2000), deleted: false, created_at: iso(now()) };
      S.comments.push(c); S.posts.find(p => p.id === c.post_id).comment_count++; save();
      return Object.assign({}, c, { author: pub(m) });
    },
    async deleteComment(id) { needMember(); const m = me(); const c = S.comments.find(x => x.id === id); if (c && (c.author_id === m.id || m.is_admin)) { c.deleted = true; const p = S.posts.find(x => x.id === c.post_id); if (p) p.comment_count = Math.max(0, p.comment_count - 1); save(); } },
    async toggleLike(postId) {
      needMember(); const m = me(); const p = S.posts.find(x => x.id === Number(postId)); const i = S.likes.findIndex(l => l.user_id === m.id && l.post_id === p.id);
      if (i >= 0) { S.likes.splice(i, 1); p.like_count = Math.max(0, p.like_count - 1); } else { S.likes.push({ user_id: m.id, post_id: p.id }); p.like_count++; }
      save(); return { liked: i < 0, count: p.like_count };
    },
    async report(type, id, reason) { needMember(); S.reports.push({ id: S.next.report++, reporter_id: me().id, target_type: type, target_id: String(id), reason: String(reason || 'No reason given').slice(0, 500), resolved: false, created_at: iso(now()) }); save(); return true; },

    // ---- friends and messages
    _fr(m) { return S.friendships.filter(f => f.requester_id === m.id || f.addressee_id === m.id); },
    async listFriends() {
      needMember(); botTick(); const m = me(); const fr = api._fr(m); const other = f => byId(f.requester_id === m.id ? f.addressee_id : f.requester_id);
      return delay({
        friends: fr.filter(f => f.status === 'accepted').map(f => ({ id: f.id, user: pub(other(f)) })),
        incoming: fr.filter(f => f.status === 'pending' && f.addressee_id === m.id).map(f => ({ id: f.id, user: pub(other(f)) })),
        outgoing: fr.filter(f => f.status === 'pending' && f.requester_id === m.id).map(f => ({ id: f.id, user: pub(other(f)) })),
        blocked: S.blocks.filter(b => b.blocker_id === m.id).map(b => pub(byId(b.blocked_id))),
      });
    },
    async friendStatus(userId) { needMember(); const m = me(); const f = api._fr(m).find(x => x.requester_id === userId || x.addressee_id === userId); if (!f) return { state: 'none' }; if (f.status === 'accepted') return { state: 'friends', id: f.id }; return { state: f.requester_id === m.id ? 'sent' : 'received', id: f.id }; },
    async searchUsers(q) { needMember(); q = String(q || '').toLowerCase().trim(); if (!q) return []; return S.users.filter(u => u.id !== me().id && u.username.startsWith(q)).slice(0, 8).map(pub); },
    async sendRequest(userId) {
      needMember(); const m = me(); if (userId === m.id) throw err('That is you!');
      if (api._fr(m).some(x => x.requester_id === userId || x.addressee_id === userId)) throw err('You are already connected or waiting for a reply.');
      const to = byId(userId); S.friendships.push({ id: S.next.fr++, requester_id: m.id, addressee_id: userId, status: 'pending', created_at: iso(now()), botAt: to && to.seed ? now() + 2500 : 0 }); save(); return true;
    },
    async respondRequest(id, accept) { needMember(); const f = S.friendships.find(x => x.id === id && x.addressee_id === me().id); if (!f) return; if (accept) f.status = 'accepted'; else S.friendships.splice(S.friendships.indexOf(f), 1); save(); },
    async removeFriendship(id) { needMember(); const m = me(); const i = S.friendships.findIndex(x => x.id === id && (x.requester_id === m.id || x.addressee_id === m.id)); if (i >= 0) S.friendships.splice(i, 1); save(); },
    async block(userId) { needMember(); const m = me(); if (!S.blocks.some(b => b.blocker_id === m.id && b.blocked_id === userId)) S.blocks.push({ blocker_id: m.id, blocked_id: userId }); const i = S.friendships.findIndex(x => (x.requester_id === m.id && x.addressee_id === userId) || (x.requester_id === userId && x.addressee_id === m.id)); if (i >= 0) S.friendships.splice(i, 1); save(); },
    async unblock(userId) { needMember(); const m = me(); S.blocks = S.blocks.filter(b => !(b.blocker_id === m.id && b.blocked_id === userId)); save(); },
    async conversations() {
      needMember(); botTick(); const m = me(); const map = new Map();
      for (const x of S.messages) { if (x.sender_id !== m.id && x.recipient_id !== m.id) continue; const o = x.sender_id === m.id ? x.recipient_id : x.sender_id; const c = map.get(o) || { user: pub(byId(o)), last: null, unread: 0 }; c.last = x; if (x.recipient_id === m.id && !x.read_at) c.unread++; map.set(o, c); }
      return [...map.values()].sort((a, b) => Date.parse(b.last.created_at) - Date.parse(a.last.created_at));
    },
    async messages(userId) { needMember(); botTick(); const m = me(); return S.messages.filter(x => (x.sender_id === m.id && x.recipient_id === userId) || (x.sender_id === userId && x.recipient_id === m.id)).map(x => ({ id: x.id, mine: x.sender_id === m.id, body: x.body, created_at: x.created_at })); },
    async sendMessage(userId, body) {
      needMember(); const m = me(); body = String(body || '').trim(); if (!body) return;
      const f = api._fr(m).find(x => x.status === 'accepted' && (x.requester_id === userId || x.addressee_id === userId));
      if (!f) throw err('You can only message friends.');
      if (S.blocks.some(b => (b.blocker_id === m.id && b.blocked_id === userId) || (b.blocker_id === userId && b.blocked_id === m.id))) throw err('You cannot message this person.');
      const msg = { id: S.next.msg++, sender_id: m.id, recipient_id: userId, body: body.slice(0, 2000), read_at: null, created_at: iso(now()), botReplyAt: byId(userId).seed ? now() + 2500 : 0 };
      S.messages.push(msg); save(); return true;
    },
    async markRead(userId) { needMember(); const m = me(); S.messages.forEach(x => { if (x.recipient_id === m.id && x.sender_id === userId && !x.read_at) x.read_at = iso(now()); }); save(); },
    async createShare() { throw err('Not available in demo mode'); },
    async getShare() { return null; },
    async setNamedSetlist() {},
    async getNamedSetlist() { return null; },
    async counts() { needMember(); botTick(); const m = me(); return { unread: S.messages.filter(x => x.recipient_id === m.id && !x.read_at).length, requests: S.friendships.filter(f => f.status === 'pending' && f.addressee_id === m.id).length }; },

    // ---- admin
    async adminResets() { const m = me(); if (!m.is_admin) throw err('Admins only'); return S.resets.filter(r => r.status === 'pending').map(r => Object.assign({}, r, { user: pub(byId(r.user_id)) })); },
    async adminResolveReset(id, approve) { const m = me(); if (!m.is_admin) throw err('Admins only'); const r = S.resets.find(x => x.id === id); if (!r) return; r.status = approve ? 'approved' : 'declined'; if (approve) { S.devices = S.devices.filter(d => d.userId !== r.user_id); delete S.active[r.user_id]; } save(); },
    async adminReports() { const m = me(); if (!m.is_admin) throw err('Admins only'); return S.reports.filter(r => !r.resolved).map(r => Object.assign({}, r, { reporter: pub(byId(r.reporter_id)) })); },
    async getSetlist() { return null; },
    async setSetlist() {},
    async adminResolveReport(id) { const m = me(); if (!m.is_admin) throw err('Admins only'); const r = S.reports.find(x => x.id === id); if (r) { r.resolved = true; save(); } },
    async deleteAccount() { const m = me(); S.accounts = S.accounts.filter(a => a.id !== m.id); S.users = S.users.filter(u => u.id !== m.id); S.devices = S.devices.filter(d => d.userId !== m.id); delete S.active[m.id]; delete S.subs[m.id]; S.posts.forEach(p => { if (p.author_id === m.id) p.deleted = true; }); S.current = null; save(); },

    // ---- demo-only controls (shown in the demo bar)
    demo: {
      reset() { localStorage.removeItem(KEY); sessionStorage.removeItem('psb.demo.device'); },
      pretendOtherDevice() { sessionStorage.setItem('psb.demo.device', 'pretend-' + Math.random().toString(16).slice(2, 14) + '-device'); },
      backToThisDevice() { sessionStorage.removeItem('psb.demo.device'); },
      isPretending() { return !!sessionStorage.getItem('psb.demo.device'); },
      loginElsewhere() { const m = me(); if (!m) return; S.active[m.id] = { sessionId: 'other-' + uid(), deviceId: 'phone', label: 'Chrome on Android', last_seen: now() }; save(); },
      makeAdmin() { const m = me(); if (m) { m.is_admin = true; save(); } },
    },
  };
  window.PSB_MOCK = api;
})();
