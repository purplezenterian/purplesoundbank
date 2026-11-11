// Pages: hub, posts, profiles, friends, messages, play, admin.
(function () {
  const A = window.PSB_APP, { h, api, C, state: S } = A;
  const R = A.routes;
  const CATS = [['all', 'All'], ['general', 'General'], ['questions', 'Questions'], ['patches', 'Patches & tips'], ['gospel', 'Styles & genres'], ['showtell', 'Show & tell']];
  const catName = k => (CATS.find(c => c[0] === k) || [0, k])[1];
  const meId = () => S.profile && S.profile.id;
  const ask = (title, text, label, fn) => A.modal(h('div', null, h('h2', null, title), h('p', { class: 'muted' }, text), h('div', { class: 'row' }, h('button', { class: 'btn danger', onclick: async () => { try { await fn(); A.closeModal(); } catch (e) { A.fail(e); } } }, label), h('button', { class: 'btn', onclick: A.closeModal }, 'Cancel'))));
  const reportDlg = (type, id) => A.modal(h('div', null, h('h2', null, 'Report'), h('p', { class: 'muted small' }, 'Tell the admins what is wrong. They will take a look.'), A.form([{ name: 'reason', label: 'What is the problem?', type: 'textarea', max: 500 }], 'Send report', async v => { await api.report(type, id, v.reason); A.closeModal(); A.toast('Thanks, the admins will take a look'); })));

  /* ---------- hub ---------- */
  const linkOnly = b => { const m = /^\s*(https?:\/\/\S+)\s*$/.exec(b || ''); if (!m) return null; const base = location.origin + location.pathname; return m[1].startsWith(base) ? { href: m[1].slice(base.length) || '#/', ext: false } : { href: m[1], ext: true }; };
  function postCard(p) {
    const lo = linkOnly(p.body);
    return h('a', Object.assign({ class: 'post' + (p.pinned ? ' pinned' : ''), href: lo ? lo.href : '#/hub/post/' + p.id }, lo && lo.ext ? { target: '_blank', rel: 'noopener noreferrer' } : {}),
      h('div', { class: 'meta' }, p.pinned ? h('span', { class: 'pill pin' }, 'Pinned') : null, h('span', { class: 'pill' }, catName(p.category)), h('span', null, p.author.display_name || p.author.username), h('span', null, A.ago(p.created_at))),
      h('h3', null, p.title), h('p', null, p.body),
      h('div', { class: 'meta', style: 'margin-top:8px' }, h('span', null, '♥ ' + p.like_count), h('span', null, '💬 ' + p.comment_count)));
  }
  R.hub = async segs => {
    if (segs[0] === 'new') return newPost();
    if (segs[0] === 'post') return postPage(segs[1]);
    A.setTitle('Hub');
    const cat = A.query.get('c') || 'all';
    const list = h('div'); const moreWrap = h('div', { class: 'center' });
    A.render(h('div', { class: 'row', style: 'justify-content:space-between;margin-bottom:12px' }, h('h1', { style: 'font-size:30px;margin:0' }, 'The hub'), h('span', null, h('a', { class: 'btn', href: '#/play', style: 'margin-right:8px' }, 'Open the instrument'), h('a', { class: 'btn primary', href: '#/hub/new' }, 'New post'))),
      h('div', { class: 'tabs', role: 'tablist' }, CATS.map(c => h('a', { class: 'tab' + (c[0] === cat ? ' on' : ''), href: '#/hub' + (c[0] === 'all' ? '' : '?c=' + c[0]), role: 'tab' }, c[1]))), list, moreWrap);
    let before = null;
    const load = async () => {
      const r = await api.listPosts({ category: cat === 'all' ? null : cat, before });
      if (!before) { (r.pinned || []).forEach(p => list.append(postCard(p))); if (!r.posts.length && !(r.pinned || []).length) list.append(h('div', { class: 'empty' }, 'No posts here yet. Be the first!')); }
      r.posts.forEach(p => list.append(postCard(p)));
      if (r.posts.length) before = r.posts[r.posts.length - 1].created_at;
      moreWrap.replaceChildren(r.more ? h('button', { class: 'btn', onclick: () => load().catch(A.fail) }, 'Load more') : null);
    };
    await load();
  };
  function newPost() {
    A.setTitle('New post');
    const f = A.form([{ name: 'category', label: 'Where does it belong?', type: 'select', options: CATS.slice(1), value: A.query.get('c') || 'general' }, { name: 'title', label: 'Title', max: 120 }, { name: 'body', label: 'What do you want to say?', type: 'textarea', max: 5000 }], 'Post', async v => {
      if (!v.title.trim() || !v.body.trim()) throw new Error('Add a title and some text');
      const p = await api.createPost(v); A.go('#/hub/post/' + p.id); });
    A.render(h('div', { class: 'card mid' }, h('a', { href: '#/hub', class: 'small' }, '← Back to the hub'), h('h2', null, 'New post'), f));
  }
  async function postPage(id) {
    const p = await api.getPost(id);
    if (!p) return A.render(h('div', { class: 'empty' }, h('h2', null, 'That post is gone'), h('a', { class: 'btn', href: '#/hub' }, 'Back to the hub')));
    A.setTitle(p.title);
    const isAdmin = S.profile && S.profile.is_admin, mine = p.author.id === meId();
    const likeBtn = h('button', { class: 'like' + (p.liked ? ' on' : ''), onclick: async () => { try { const r = await api.toggleLike(p.id); p.liked = r.liked; p.like_count = r.count; paint(); } catch (e) { A.fail(e); } } });
    const paint = () => { likeBtn.className = 'like' + (p.liked ? ' on' : ''); likeBtn.textContent = (p.liked ? '♥ ' : '♡ ') + p.like_count; }; paint();
    const cbox = h('div'); const cform = h('div');
    const draw = async () => {
      const cs = await api.listComments(p.id);
      const tops = cs.filter(c => !c.parent_id), kids = pid => cs.filter(c => c.parent_id === pid);
      const one = (c, reply) => h('div', { class: 'comment' + (reply ? ' reply' : '') },
        h('div', { class: 'meta' }, A.who(c.author), h('span', null, A.ago(c.created_at))), h('div', { class: 'body', style: 'margin:6px 0' }, c.body),
        h('div', null, !reply ? h('button', { class: 'linkbtn', onclick: () => replyTo(c) }, 'Reply') : null, c.author.id !== meId() ? h('button', { class: 'linkbtn', onclick: () => reportDlg('comment', c.id) }, 'Report') : null,
          (c.author.id === meId() || isAdmin) ? h('button', { class: 'linkbtn', onclick: () => ask('Delete comment?', 'This cannot be undone.', 'Delete', async () => { await api.deleteComment(c.id); draw(); }) }, 'Delete') : null));
      cbox.replaceChildren(h('h3', null, cs.length + (cs.length === 1 ? ' comment' : ' comments')), ...tops.map(c => h('div', null, one(c), ...kids(c.id).map(k => one(k, true)))));
    };
    const box = (parentId, label) => A.form([{ name: 'body', label, type: 'textarea', max: 2000 }], parentId ? 'Reply' : 'Comment', async v => {
      if (!v.body.trim()) throw new Error('Write something first'); await api.addComment(p.id, v.body, parentId || null); await draw(); mainForm(); });
    const mainForm = () => cform.replaceChildren(box(null, 'Add a comment'));
    const replyTo = c => A.modal(h('div', null, h('h2', null, 'Reply to ' + (c.author.display_name || c.author.username)), h('p', { class: 'muted small body' }, c.body.slice(0, 160)), A.form([{ name: 'body', label: 'Your reply', type: 'textarea', max: 2000 }], 'Reply', async v => { if (!v.body.trim()) throw new Error('Write something first'); await api.addComment(p.id, v.body, c.id); A.closeModal(); draw(); })));
    A.render(h('div', { class: 'mid' }, h('a', { href: '#/hub', class: 'small' }, '← Back to the hub'),
      h('div', { class: 'card', style: 'margin-top:10px' }, h('div', { class: 'meta' }, p.pinned ? h('span', { class: 'pill pin' }, 'Pinned') : null, h('span', { class: 'pill' }, catName(p.category)), A.who(p.author), h('span', null, A.ago(p.created_at))),
        h('h1', { style: 'font-size:28px;margin:10px 0' }, p.title), (() => { const lo = linkOnly(p.body); return lo ? h('a', Object.assign({ class: 'btn primary', href: lo.href }, lo.ext ? { target: '_blank', rel: 'noopener noreferrer' } : {}), 'Open') : h('div', { class: 'body' }, p.body); })(),
        h('div', { class: 'row', style: 'margin-top:14px' }, likeBtn, !mine ? h('button', { class: 'linkbtn', onclick: () => reportDlg('post', p.id) }, 'Report') : null,
          (mine || isAdmin) ? h('button', { class: 'linkbtn', onclick: () => ask('Delete post?', 'The post and its comments will be removed.', 'Delete', async () => { await api.deletePost(p.id); A.go('#/hub'); }) }, 'Delete') : null,
          isAdmin ? h('button', { class: 'linkbtn', onclick: async () => { await api.pinPost(p.id, !p.pinned); A.route(); } }, p.pinned ? 'Unpin' : 'Pin') : null)),
      h('div', { class: 'card', style: 'margin-top:14px' }, cbox, h('div', { style: 'margin-top:14px' }, cform))));
    await draw(); mainForm();
  }

  /* ---------- profile ---------- */
  const COLORS = ['#7b2fff', '#b04bff', '#ff4b9a', '#ff7a45', '#2fb8ff', '#2fd58a', '#e0b62f', '#8892a6'];
  R.u = async segs => {
    const name = segs[0]; const u = await api.getProfile(name);
    if (!u) return A.render(h('div', { class: 'empty' }, h('h2', null, 'No one with that name'), h('a', { class: 'btn', href: '#/friends' }, 'Find people')));
    A.setTitle(u.display_name || u.username);
    const mine = u.id === meId(); const card = h('div', { class: 'card mid' });
    A.render(card);
    const head = () => h('div', { class: 'row', style: 'gap:16px;margin-bottom:14px' }, A.avatar(u, 'lg'), h('div', null, h('h1', { style: 'font-size:28px;margin:0' }, u.display_name || u.username), h('div', { class: 'muted' }, '@' + u.username + ' · joined ' + A.date(u.created_at))));
    if (mine) {
      let color = u.avatar_color;
      const sw = h('div', { class: 'row', style: 'margin-bottom:12px' }, COLORS.map(c => h('button', { type: 'button', 'aria-label': 'Colour ' + c, style: 'width:30px;height:30px;border-radius:50%;border:2px solid ' + (c === color ? '#fff' : 'transparent') + ';background:' + c + ';cursor:pointer', onclick: ev => { color = c; [...sw.children].forEach(b => b.style.borderColor = 'transparent'); ev.currentTarget.style.borderColor = '#fff'; } })));
      card.append(head(), h('h3', null, 'Edit your profile'), h('label', null, 'Avatar colour'), sw,
        A.form([{ name: 'display_name', label: 'Display name', value: u.display_name, max: 40 }, { name: 'bio', label: 'About you (what you play, your setup…)', type: 'textarea', value: u.bio, max: 300 }], 'Save', async v => { await api.updateProfile({ display_name: v.display_name, bio: v.bio, avatar_color: color }); await A.refreshState(); A.toast('Saved'); A.route(); }));
      return;
    }
    const fs = await api.friendStatus(u.id); const blocked = (await api.listFriends()).blocked.some(b => b.id === u.id || (b.user && b.user.id === u.id));
    const row = h('div', { class: 'row' });
    const act = (label, fn, cls) => h('button', { class: 'btn ' + (cls || ''), onclick: async () => { try { await fn(); A.route(); } catch (e) { A.fail(e); } } }, label);
    if (blocked) row.append(h('span', { class: 'muted' }, 'You have blocked this person.'), act('Unblock', () => api.unblock(u.id)));
    else {
      if (fs.state === 'none') row.append(act('Add friend', async () => { await api.sendRequest(u.id); A.toast('Request sent'); }, 'primary'));
      if (fs.state === 'sent') row.append(h('span', { class: 'pill' }, 'Request sent'), act('Cancel', () => api.removeFriendship(fs.id)));
      if (fs.state === 'received') row.append(act('Accept request', () => api.respondRequest(fs.id, true), 'primary'), act('Decline', () => api.respondRequest(fs.id, false)));
      if (fs.state === 'friends') row.append(h('a', { class: 'btn primary', href: '#/messages/' + u.username }, 'Message'), act('Unfriend', () => api.removeFriendship(fs.id)));
      row.append(h('button', { class: 'btn', onclick: () => reportDlg('user', u.id) }, 'Report'), h('button', { class: 'btn', onclick: () => ask('Block ' + u.username + '?', 'They will not be able to message you or send a friend request, and you will be unfriended.', 'Block', async () => { await api.block(u.id); A.route(); }) }, 'Block'));
    }
    card.append(head(), u.bio ? h('p', { class: 'body' }, u.bio) : h('p', { class: 'muted' }, 'No bio yet.'), row);
  };

  /* ---------- friends ---------- */
  R.friends = async () => {
    A.setTitle('Friends');
    const lists = h('div'); const results = h('div');
    const draw = async () => {
      const d = await api.listFriends(); await A.refreshState();
      const person = (u, ...btns) => h('div', { class: 'list-row' }, h('div', { class: 'grow' }, A.who(u)), ...btns);
      const B = (t, fn, cls) => h('button', { class: 'btn small ' + (cls || ''), onclick: async () => { try { await fn(); await draw(); } catch (e) { A.fail(e); } } }, t);
      lists.replaceChildren(
        d.incoming.length ? h('div', { class: 'card', style: 'margin-bottom:14px' }, h('h3', null, 'Friend requests'), d.incoming.map(r => person(r.user, B('Accept', () => api.respondRequest(r.id, true), 'primary'), B('Decline', () => api.respondRequest(r.id, false))))) : null,
        h('div', { class: 'card', style: 'margin-bottom:14px' }, h('h3', null, 'Your friends (' + d.friends.length + ')'), d.friends.length ? d.friends.map(r => person(r.user, h('a', { class: 'btn small primary', href: '#/messages/' + r.user.username }, 'Message'), B('Unfriend', () => api.removeFriendship(r.id)))) : h('p', { class: 'muted' }, 'No friends yet. Search above, or say hi in the ', h('a', { href: '#/hub' }, 'hub'), '.')),
        d.outgoing.length ? h('div', { class: 'card', style: 'margin-bottom:14px' }, h('h3', null, 'Waiting for a reply'), d.outgoing.map(r => person(r.user, B('Cancel', () => api.removeFriendship(r.id))))) : null,
        d.blocked.length ? h('div', { class: 'card' }, h('h3', null, 'Blocked'), d.blocked.map(b => { const u = b.user || b; return person(u, B('Unblock', () => api.unblock(u.id))); })) : null);
    };
    const search = h('input', { type: 'search', placeholder: 'Search by username…', 'aria-label': 'Search people', maxlength: 20, autocomplete: 'off' });
    let t; search.addEventListener('input', () => { clearTimeout(t); t = setTimeout(async () => {
      const q = search.value.trim(); if (!q) return results.replaceChildren();
      try { const us = await api.searchUsers(q); results.replaceChildren(h('div', { class: 'card', style: 'margin-top:10px' }, us.length ? us.map(u => h('div', { class: 'list-row' }, h('div', { class: 'grow' }, A.who(u)), h('a', { class: 'btn small', href: '#/u/' + u.username }, 'View'))) : h('p', { class: 'muted', style: 'margin:0' }, 'No one found.'))); } catch (e) { A.fail(e); } }, 250); });
    A.render(h('div', { class: 'mid' }, h('h1', { style: 'font-size:30px' }, 'Friends'), h('div', { class: 'field' }, search), results, h('div', { style: 'margin-top:18px' }, lists)));
    await draw();
  };

  /* ---------- messages ---------- */
  R.messages = async segs => {
    A.setTitle('Messages');
    const withName = segs[0]; let other = null;
    if (withName) { other = await api.getProfile(withName); if (!other) return A.go('#/messages'); }
    const convs = await api.conversations();
    const list = h('div', { class: 'convs' }, convs.length ? convs.map(c => h('a', { class: 'conv' + (other && other.id === c.user.id ? ' on' : ''), href: '#/messages/' + c.user.username }, A.avatar(c.user, 'sm'), h('div', { class: 't' }, h('div', null, c.user.display_name || c.user.username), h('div', { class: 'last' }, (c.last.sender_id === meId() ? 'You: ' : '') + c.last.body)), c.unread ? h('span', { class: 'badge' }, c.unread) : null)) : h('div', { class: 'empty small' }, 'No conversations yet.'));
    let pane;
    if (!other) pane = h('div', { class: 'thread' }, h('div', { class: 'empty', style: 'margin:auto' }, h('p', null, 'Pick a conversation, or message a friend from your '), h('a', { class: 'btn', href: '#/friends' }, 'friends list')));
    else {
      const bubbles = h('div', { class: 'bubbles', 'aria-live': 'polite' }); let lastCount = -1;
      const input = h('input', { type: 'text', maxlength: 2000, placeholder: 'Write a message…', 'aria-label': 'Message' });
      const err = h('div');
      const pull = async () => {
        const ms = await api.messages(other.id); if (ms.length === lastCount) return; const stick = bubbles.scrollTop + bubbles.clientHeight >= bubbles.scrollHeight - 40 || lastCount < 0; lastCount = ms.length;
        bubbles.replaceChildren(...ms.map(m => h('div', { class: 'bubble' + (m.mine ? ' mine' : '') }, m.body, h('time', null, A.ago(m.created_at)))));
        if (!ms.length) bubbles.append(h('div', { class: 'empty small' }, 'Say hello to ' + (other.display_name || other.username) + '!'));
        if (stick) bubbles.scrollTop = bubbles.scrollHeight; await api.markRead(other.id); A.refreshState().catch(() => {});
      };
      const send = async ev => { ev.preventDefault(); const v = input.value.trim(); if (!v) return; err.replaceChildren(); try { await api.sendMessage(other.id, v); input.value = ''; await pull(); } catch (e) { if (e.code === 'other_device') return A.fail(e); err.replaceChildren(h('div', { class: 'msg err', style: 'margin:0 10px' }, e.message)); } };
      pane = h('div', { class: 'thread' }, h('div', { class: 'thread-head' }, A.who(other)), bubbles, err, h('form', { class: 'compose', onsubmit: send }, input, h('button', { class: 'btn primary', type: 'submit' }, 'Send')));
      await pull(); A.every(() => pull().catch(() => {}), 4000);
    }
    A.render(h('h1', { style: 'font-size:30px' }, 'Messages'), h('div', { class: 'chat' }, list, pane));
  };

  /* ---------- play ---------- */
  R.play = async () => {
    A.setTitle('Play');
    const bar = h('div', { class: 'loadbar' }, h('i')); const status = h('p', { class: 'muted center' }, 'Preparing your instrument…');
    A.render(h('div', { class: 'card narrow center' }, h('h2', null, 'Opening the instrument'), status, bar, h('p', { class: 'small muted' }, 'Tip: use Chrome or Edge to play with a MIDI keyboard.')));
    let url = await api.instrumentUrl(), src = url, srcdoc = null, blobUrl = null;
    if (C.mode !== 'mock') {
      const res = await fetch(url); if (!res.ok) throw new Error('Could not load the instrument (' + res.status + ')');
      const total = +res.headers.get('content-length') || 0, rd = res.body.getReader(), parts = []; let got = 0;
      for (;;) { const { done, value } = await rd.read(); if (done) break; parts.push(value); got += value.length; if (total) bar.firstChild.style.width = Math.round(got / total * 100) + '%'; status.textContent = 'Loading sounds… ' + (got / 1048576).toFixed(1) + ' MB'; }
      srcdoc = await new Blob(parts, { type: 'text/html' }).text(); src = null;
    }
    try { const sl = await api.getSetlist(); if (sl && sl.data && localStorage.getItem('site.setv') !== String(sl.v)) { Object.keys(sl.data).forEach(k => { if (k.startsWith('psb.')) localStorage.setItem(k, sl.data[k]); }); localStorage.setItem('site.setv', String(sl.v)); } } catch (e) {}
    try { const d = window.PSB_DEFAULTS || {}; Object.keys(d).forEach(k => { localStorage.setItem(k, d[k]); }); } catch (e) {}
    document.body.classList.add('playing');
    const wrap = h('div', { class: 'play-wrap' }, h('div', { class: 'play-bar' }, h('b', null, C.siteName), h('span', { class: 'muted grow' }, 'One device at a time: signing in elsewhere will end this session.'),
      h('a', { class: 'btn small', href: '#/hub' }, 'Hub'), h('button', { class: 'btn small', onclick: () => leave('#/account') }, 'Account')),
      h('iframe', Object.assign(srcdoc !== null ? { srcdoc } : { src }, { title: 'Purple SoundBank', allow: 'midi; autoplay; fullscreen; microphone', allowfullscreen: 'true' })));
    const leave = to => { cleanup(); A.go(to); };
    const cleanup = () => { wrap.remove(); document.body.classList.remove('playing'); if (blobUrl) URL.revokeObjectURL(blobUrl); window.removeEventListener('hashchange', cleanup); };
    window.addEventListener('hashchange', cleanup);
    document.body.append(wrap);
  };

  /* ---------- one-tap export of this browser's instrument settings ---------- */
  R.export = async () => {
    A.setTitle('Export');
    const o = {}; for (let i = 0; i < localStorage.length; i++) { const k = localStorage.key(i); if (k.startsWith('psb.')) o[k] = localStorage.getItem(k); }
    const txt = JSON.stringify(o), ta = h('textarea', { rows: 8, style: 'width:100%' }); ta.value = txt;
    A.render(h('div', { class: 'card narrow center' }, h('h2', null, 'Your saved songs'), h('p', { class: 'muted' }, Object.keys(o).length + ' items found'),
      h('button', { class: 'btn', onclick: async () => { ta.select(); try { await navigator.clipboard.writeText(txt); } catch (e) { document.execCommand('copy'); } A.toast && A.toast('Copied'); } }, 'Copy'), (A.state && A.state.profile && A.state.profile.is_admin) || true ? h('button', { class: 'btn primary', style: 'margin-left:8px', onclick: async () => { try { await api.setSetlist(txt); alert('Published. Members get it next time they press Play.'); } catch (e) { A.fail(e); } } }, 'Publish to members') : null, ta));
  };

  /* ---------- admin ---------- */
  R.admin = async () => {
    A.setTitle('Admin');
    const wrap = h('div', { class: 'mid' });
    const draw = async () => {
      const [rs, rp] = await Promise.all([api.adminResets(), api.adminReports()]);
      const B = (t, fn, cls) => h('button', { class: 'btn small ' + (cls || ''), onclick: async () => { try { await fn(); await draw(); await A.refreshState(); } catch (e) { A.fail(e); } } }, t);
      wrap.replaceChildren(h('h1', { style: 'font-size:30px' }, 'Admin'),
        h('div', { class: 'card', style: 'margin-bottom:14px' }, h('h3', null, 'Set list for members'), h('p', { class: 'small muted' }, 'Open #/export on your device, Copy, paste here, Publish. Members get it next time they press Play.'), (() => { const t = h('textarea', { rows: 4, style: 'width:100%' }); return h('div', null, t, h('button', { class: 'btn primary small', onclick: async () => { try { await api.setSetlist(t.value.trim()); alert('Published'); } catch (e) { A.fail(e); } } }, 'Publish')); })()),
        h('div', { class: 'card', style: 'margin-bottom:14px' }, h('h3', null, 'Device change requests (' + rs.length + ')'), h('p', { class: 'small muted' }, 'Members who lost or replaced a device. Approving frees their device slots so they can sign in on the new one.'),
          rs.length ? rs.map(r => h('div', { class: 'list-row' }, h('div', { class: 'grow' }, A.who(r.user), h('div', { class: 'small muted' }, (r.reason || 'No reason') + ' · ' + A.ago(r.created_at))), B('Approve', () => api.adminResolveReset(r.id, true), 'primary'), B('Decline', () => api.adminResolveReset(r.id, false)))) : h('p', { class: 'muted' }, 'Nothing waiting.')),
        h('div', { class: 'card' }, h('h3', null, 'Reports (' + rp.length + ')'), rp.length ? rp.map(r => h('div', { class: 'list-row' }, h('div', { class: 'grow' }, h('b', null, r.target_type + ' #' + r.target_id), h('div', { class: 'small' }, r.reason), h('div', { class: 'small muted' }, 'by ' + ((r.reporter && r.reporter.username) || '?') + ' · ' + A.ago(r.created_at))),
          r.target_type === 'post' ? h('a', { class: 'btn small', href: '#/hub/post/' + r.target_id }, 'Open') : r.target_type === 'user' ? h('a', { class: 'btn small', href: '#/u/' + (r.target_username || '') }, 'Open') : null, B('Resolve', () => api.adminResolveReport(r.id), 'primary'))) : h('p', { class: 'muted' }, 'No open reports.')));
    };
    A.render(wrap); await draw();
  };
})();
