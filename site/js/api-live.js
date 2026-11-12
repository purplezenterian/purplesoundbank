// LIVE backend: Supabase (accounts, database) + Stripe (payments, through the edge functions).
(function () {
  const C = window.PSB_CONFIG;
  let sb = null;
  let session = null;

  const err = (m, code) => { const e = new Error(m); e.code = code || 'error'; return e; };
  const unwrap = r => { if (r.error) { const m = r.error.message || 'Something went wrong'; const e = err(m, r.error.code); throw e; } return r.data; };
  const b64 = s => { s = s.replace(/-/g, '+').replace(/_/g, '/'); while (s.length % 4) s += '='; return atob(s); };
  const sessionIdOf = tok => { try { return JSON.parse(b64(tok.split('.')[1])).session_id || ''; } catch (e) { return ''; } };

  async function token() { const { data } = await sb.auth.getSession(); session = data.session; return session ? session.access_token : null; }

  async function fn(name, body) {
    const t = await token(); if (!t) throw err('Please sign in', 'auth');
    const r = await fetch(C.supabaseUrl.replace(/\/$/, '') + '/functions/v1/' + name, {
      method: 'POST', headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + t, apikey: C.supabaseAnonKey }, body: JSON.stringify(body || {}),
    });
    let data = {}; try { data = await r.json(); } catch (e) {}
    return { ok: r.ok, status: r.status, data };
  }
  async function fnOrThrow(name, body) {
    const r = await fn(name, body);
    if (!r.ok) throw err(r.data.error === 'other_device' ? 'other_device' : (r.data.error || 'Something went wrong'), r.data.error === 'other_device' ? 'other_device' : r.data.error === 'no_subscription' ? 'no_subscription' : 'error');
    return r.data;
  }

  const AUTHOR = 'author:profiles!posts_author_id_fkey(id,username,display_name,avatar_color,bio,created_at)';
  const prof = 'id,username,display_name,bio,avatar_color,created_at,is_admin';
  const me = async () => { await token(); return session ? session.user.id : null; };
  const mapErr = e => { if (e && e.message && /row-level security|permission denied/i.test(e.message)) return err('That is not available right now. If you were signed in elsewhere, please refresh.', 'no_access'); return e; };
  async function q(p) { try { return unwrap(await p); } catch (e) { throw mapErr(e); } }

  const api = {
    mode: 'live',
    async init() {
      if (!window.supabase) await new Promise((res, rej) => { const s = document.createElement('script'); s.src = 'https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.45.4/dist/umd/supabase.js'; s.onload = res; s.onerror = () => rej(err('Could not load the sign-in service. Check your connection.')); document.head.appendChild(s); });
      sb = window.supabase.createClient(C.supabaseUrl, C.supabaseAnonKey, { auth: { persistSession: true, autoRefreshToken: true, detectSessionInUrl: true } });
    },
    async getSession() { await token(); return session ? { user: { id: session.user.id, email: session.user.email }, sessionId: sessionIdOf(session.access_token) } : null; },
    async signUp({ email, password, username }) {
      username = String(username || '').trim().toLowerCase();
      if (!/^[a-z0-9_]{3,20}$/.test(username)) throw err('Usernames are 3-20 letters, numbers or underscores');
      const { data, error } = await sb.auth.signUp({ email: String(email).trim(), password, options: { data: { username }, emailRedirectTo: location.origin + location.pathname } });
      if (error) throw err(/Database error/i.test(error.message) ? 'That username is taken (or not allowed). Try another.' : error.message);
      return { needsConfirm: !data.session };
    },
    async signIn(email, password) { const { error } = await sb.auth.signInWithPassword({ email: String(email).trim(), password }); if (error) throw err(error.message); return true; },
    async signOut() { await sb.auth.signOut(); session = null; },
    onRecovery(cb) { sb.auth.onAuthStateChange(ev => { if (ev === 'PASSWORD_RECOVERY') cb(); }); },
    async updatePassword(pw) { const { error } = await sb.auth.updateUser({ password: pw }); if (error) throw err(error.message); return true; },
    async resetPassword(email) { const { error } = await sb.auth.resetPasswordForEmail(String(email).trim(), { redirectTo: location.origin + location.pathname }); if (error) throw err(error.message); return true; },

    async registerDevice(takeover) {
      const r = await fn('register-device', { deviceId: await PSB_DEVICE.deviceId(), fp: PSB_DEVICE.fingerprint(), label: PSB_DEVICE.label(), takeover: !!takeover });
      if (r.data && r.data.status) return r.data;
      throw err((r.data && r.data.error) || 'Could not check this device');
    },
    async heartbeat() {
      const t = await token(); if (!t) return { kicked: true };
      const mine = sessionIdOf(t); const uid = session.user.id;
      const { data, error } = await sb.from('active_sessions').select('session_id').eq('user_id', uid).maybeSingle();
      if (error) return { ok: true };            // a network blip is not a reason to sign anyone out
      if (!data || data.session_id !== mine) return { kicked: true };
      await sb.from('active_sessions').update({ last_seen: new Date().toISOString() }).eq('user_id', uid);
      return { ok: true };
    },
    async myDevices() { const id = await PSB_DEVICE.deviceId(); const u = await me(); const d = await q(sb.from('devices').select('label,last_seen,device_id').eq('user_id', u)); return d.map(x => ({ label: x.label, last_seen: x.last_seen, current: x.device_id === id })); },
    async requestReset(note) { const u = await me(); const { error } = await sb.from('device_reset_requests').insert({ user_id: u, note: String(note || '').slice(0, 300) }); if (error) throw err(/duplicate/i.test(error.message) ? 'You already have a request waiting.' : error.message); return true; },
    async myResetPending() { const u = await me(); const d = await q(sb.from('device_reset_requests').select('id').eq('user_id', u).eq('status', 'pending')); return d.length > 0; },

    async getSubscription() { const u = await me(); if (!u) return null; const d = await q(sb.from('subscriptions').select('status,trial_end,current_period_end,cancel_at_period_end').eq('user_id', u).maybeSingle()); return d || null; },
    async startCheckout() { const d = await fnOrThrow('create-checkout'); return { url: d.url, trial: d.trial }; },
    async upgradeNow(card) { return fnOrThrow('upgrade-now', card ? { card } : {}); },
    async openPortal() { const d = await fnOrThrow('billing-portal'); return { url: d.url }; },
    async instrumentUrl() { const d = await fnOrThrow('get-instrument', { deviceId: await PSB_DEVICE.deviceId() }); return d.url; },

    async me() { const u = await me(); if (!u) return null; return q(sb.from('profiles').select(prof + ',trial_used').eq('id', u).single()); },
    async getProfile(username) { return q(sb.from('profiles').select(prof).eq('username', String(username).toLowerCase()).maybeSingle()); },
    async updateProfile(p) { const u = await me(); const patch = {}; ['display_name', 'bio', 'avatar_color'].forEach(k => { if (p[k] !== undefined) patch[k] = p[k]; }); return q(sb.from('profiles').update(patch).eq('id', u).select(prof).single()); },

    async _liked(ids) { if (!ids.length) return new Set(); const d = await q(sb.from('likes').select('post_id').in('post_id', ids)); return new Set(d.map(x => x.post_id)); },
    async listPosts({ category, before } = {}) {
      const cols = 'id,category,title,body,pinned,comment_count,like_count,created_at,' + AUTHOR;
      let pinned = [];
      if (!before) { let pq = sb.from('posts').select(cols).eq('deleted', false).eq('pinned', true).order('id', { ascending: false }); if (category) pq = pq.eq('category', category); pinned = await q(pq); }
      let pq2 = sb.from('posts').select(cols).eq('deleted', false).eq('pinned', false).order('id', { ascending: false }).limit(21);
      if (category) pq2 = pq2.eq('category', category); if (before) pq2 = pq2.lt('id', before);
      const rows = await q(pq2); const page = rows.slice(0, 20);
      const liked = await api._liked([...pinned, ...page].map(p => p.id));
      const fix = p => Object.assign(p, { liked: liked.has(p.id) });
      return { pinned: pinned.map(fix), posts: page.map(fix), more: rows.length > 20 };
    },
    async getPost(id) { const p = await q(sb.from('posts').select('id,category,title,body,pinned,comment_count,like_count,created_at,' + AUTHOR).eq('id', id).eq('deleted', false).maybeSingle()); if (!p) return null; p.liked = (await api._liked([p.id])).has(p.id); return p; },
    async createPost({ category, title, body }) {
      const u = await me(); title = String(title || '').trim(); body = String(body || '').trim();
      if (title.length < 3) throw err('Give your post a title (at least 3 letters)'); if (!body) throw err('Write something in your post');
      const { data, error } = await sb.from('posts').insert({ author_id: u, category, title: title.slice(0, 120), body: body.slice(0, 5000) }).select('id').single();
      if (error) throw err(error.message); return api.getPost(data.id);
    },
    async deletePost(id) { await q(sb.rpc('delete_post', { p: id })); },
    async pinPost(id, v) { await q(sb.rpc('pin_post', { p: id, v })); },
    async listComments(postId) { return q(sb.from('comments').select('id,post_id,parent_id,body,created_at,author:profiles!comments_author_id_fkey(' + prof + ')').eq('post_id', postId).eq('deleted', false).order('id', { ascending: true })); },
    async addComment(postId, body, parentId) {
      const u = await me(); body = String(body || '').trim(); if (!body) throw err('Write a comment first');
      const { data, error } = await sb.from('comments').insert({ post_id: postId, author_id: u, parent_id: parentId || null, body: body.slice(0, 2000) }).select('id,post_id,parent_id,body,created_at,author:profiles!comments_author_id_fkey(' + prof + ')').single();
      if (error) throw err(error.message); return data;
    },
    async deleteComment(id) { await q(sb.rpc('delete_comment', { c: id })); },
    async toggleLike(postId) {
      const u = await me(); const ex = await q(sb.from('likes').select('post_id').eq('post_id', postId).eq('user_id', u).maybeSingle());
      if (ex) await q(sb.from('likes').delete().eq('post_id', postId).eq('user_id', u)); else await q(sb.from('likes').insert({ post_id: postId, user_id: u }));
      const p = await q(sb.from('posts').select('like_count').eq('id', postId).single()); return { liked: !ex, count: p.like_count };
    },
    async report(type, id, reason) { const u = await me(); await q(sb.from('reports').insert({ reporter_id: u, target_type: type, target_id: String(id), reason: String(reason || 'No reason given').slice(0, 500) })); return true; },

    async listFriends() {
      const u = await me();
      const rows = await q(sb.from('friendships').select('id,status,requester_id,addressee_id,requester:profiles!friendships_requester_id_fkey(' + prof + '),addressee:profiles!friendships_addressee_id_fkey(' + prof + ')'));
      const other = f => (f.requester_id === u ? f.addressee : f.requester);
      const blocked = await q(sb.from('blocks').select('blocked:profiles!blocks_blocked_id_fkey(' + prof + ')').eq('blocker_id', u));
      return {
        friends: rows.filter(f => f.status === 'accepted').map(f => ({ id: f.id, user: other(f) })),
        incoming: rows.filter(f => f.status === 'pending' && f.addressee_id === u).map(f => ({ id: f.id, user: other(f) })),
        outgoing: rows.filter(f => f.status === 'pending' && f.requester_id === u).map(f => ({ id: f.id, user: other(f) })),
        blocked: blocked.map(b => b.blocked),
      };
    },
    async friendStatus(userId) {
      const u = await me(); const rows = await q(sb.from('friendships').select('id,status,requester_id,addressee_id').or(`and(requester_id.eq.${u},addressee_id.eq.${userId}),and(requester_id.eq.${userId},addressee_id.eq.${u})`));
      const f = rows[0]; if (!f) return { state: 'none' }; if (f.status === 'accepted') return { state: 'friends', id: f.id }; return { state: f.requester_id === u ? 'sent' : 'received', id: f.id };
    },
    async searchUsers(s) { s = String(s || '').toLowerCase().trim().replace(/[%_,()]/g, ''); if (!s) return []; const u = await me(); return q(sb.from('profiles').select(prof).ilike('username', s + '%').neq('id', u).limit(8)); },
    async sendRequest(userId) { const u = await me(); const { error } = await sb.from('friendships').insert({ requester_id: u, addressee_id: userId }); if (error) throw err(/duplicate/i.test(error.message) ? 'You are already connected or waiting for a reply.' : error.message); return true; },
    async respondRequest(id, accept) { if (accept) await q(sb.from('friendships').update({ status: 'accepted' }).eq('id', id)); else await q(sb.from('friendships').delete().eq('id', id)); },
    async removeFriendship(id) { await q(sb.from('friendships').delete().eq('id', id)); },
    async block(userId) { const u = await me(); await q(sb.from('blocks').upsert({ blocker_id: u, blocked_id: userId })); await q(sb.from('friendships').delete().or(`and(requester_id.eq.${u},addressee_id.eq.${userId}),and(requester_id.eq.${userId},addressee_id.eq.${u})`)); },
    async unblock(userId) { const u = await me(); await q(sb.from('blocks').delete().eq('blocker_id', u).eq('blocked_id', userId)); },
    async conversations() {
      const u = await me();
      const rows = await q(sb.from('messages').select('id,sender_id,recipient_id,body,read_at,created_at,sender:profiles!messages_sender_id_fkey(' + prof + '),recipient:profiles!messages_recipient_id_fkey(' + prof + ')').order('id', { ascending: false }).limit(300));
      const map = new Map();
      for (const m of rows) { const o = m.sender_id === u ? m.recipient : m.sender; const key = o.id; const c = map.get(key) || { user: o, last: m, unread: 0 }; if (m.recipient_id === u && !m.read_at) c.unread++; map.set(key, c); }
      return [...map.values()];
    },
    async messages(userId) {
      const u = await me(); const rows = await q(sb.from('messages').select('id,sender_id,body,created_at').or(`and(sender_id.eq.${u},recipient_id.eq.${userId}),and(sender_id.eq.${userId},recipient_id.eq.${u})`).order('id', { ascending: true }).limit(200));
      return rows.map(m => ({ id: m.id, mine: m.sender_id === u, body: m.body, created_at: m.created_at }));
    },
    async sendMessage(userId, body) { const u = await me(); body = String(body || '').trim(); if (!body) return; const { error } = await sb.from('messages').insert({ sender_id: u, recipient_id: userId, body: body.slice(0, 2000) }); if (error) throw err(/row-level/i.test(error.message) ? 'You can only message friends you have not blocked.' : error.message); return true; },
    async markRead(userId) { const u = await me(); await sb.from('messages').update({ read_at: new Date().toISOString() }).eq('recipient_id', u).eq('sender_id', userId).is('read_at', null); },
    async createShare(name, song) { return q(sb.rpc('create_share', { n: name, s: song })); },
    async getShare(code) { return q(sb.rpc('get_share', { c: code })); },
    async setNamedSetlist(n, txt) { await q(sb.rpc('set_named_setlist', { n, v: txt })); },
    async getNamedSetlist(n) { try { return unwrap(await sb.rpc('get_named_setlist', { n })); } catch (e) { return null; } },
    async counts() {
      const u = await me();
      const [a, b] = await Promise.all([
        sb.from('messages').select('id', { count: 'exact', head: true }).eq('recipient_id', u).is('read_at', null),
        sb.from('friendships').select('id', { count: 'exact', head: true }).eq('addressee_id', u).eq('status', 'pending'),
      ]);
      return { unread: a.count || 0, requests: b.count || 0 };
    },

    async adminResets() { return q(sb.from('device_reset_requests').select('id,user_id,note,created_at,user:profiles!device_reset_requests_user_id_fkey(' + prof + ')').eq('status', 'pending').order('id')); },
    async adminResolveReset(id, approve) { await fnOrThrow('admin-reset-device', { requestId: id, approve }); },
    async adminReports() { return q(sb.from('reports').select('id,target_type,target_id,reason,created_at,reporter:profiles!reports_reporter_id_fkey(' + prof + ')').eq('resolved', false).order('id')); },
    async getSetlist() { try { return unwrap(await sb.rpc('get_setlist')); } catch (e) { return null; } },
    async setSetlist(txt) { await q(sb.rpc('set_setlist', { v: txt })); },
    async adminResolveReport(id) { await q(sb.from('reports').update({ resolved: true }).eq('id', id)); },
    async deleteAccount() { await fnOrThrow('delete-account', { confirm: 'DELETE' }); await sb.auth.signOut(); },
    demo: null,
  };
  window.PSB_LIVE = api;
})();
