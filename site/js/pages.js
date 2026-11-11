// Pages: landing, pricing, sign-up / log-in, account, legal.
(function () {
  const A = window.PSB_APP, { h, api, C, state: S } = A;
  const R = A.routes;
  const SOUNDS = ['Purple Grand', 'Purple EP 2', 'Poly Pad', 'Multi Strings', 'D Minus 22', 'ZDK Strings', 'Z Synbrass 2', '80s Pop Brass'];

  /* ---------- landing ---------- */
  R[''] = async () => {
    A.setTitle('');
    if (S.session && A.isMember()) return A.go('#/hub');
    const cta = S.session ? h('a', { class: 'btn primary big', href: '#/pricing' }, 'Start your free month') : h('a', { class: 'btn primary big', href: '#/signup' }, 'Start your free month');
    A.render(
      h('section', { class: 'hero-brand' }, h('span', { class: 'mark big' }, h('i'), h('i'), h('i')), h('span', { class: 'name' }, C.siteName)),
      h('section', { class: 'section' }, h('div', { class: 'card glow mid center' }, h('h2', { style: 'margin:0 0 8px' }, 'New sounds and performances added every month'), h('p', { class: 'muted', style: 'margin:0' }, 'Everything is updated every month too, and it is all included in your membership.'))),
      pricingCard(true),
      h('section', { class: 'section mid' }, h('h2', { class: 'center' }, 'Questions'), faq()));
  };
  function faq() {
    const q = (a, b) => h('details', null, h('summary', null, a), h('p', null, b));
    return [q('What do I need?', 'A computer, tablet or phone with a modern browser. Chrome, Edge and Opera work with USB and Bluetooth MIDI keyboards. Safari and iPhone/iPad browsers play the on-screen keys and sounds, but they do not support MIDI keyboards.'),
      q('How does the free month work?', 'You add a card when you start, and you are not charged for 30 days. Cancel before day 30 from your account page and you pay nothing. After that it is ' + C.price + ' a month. The free month is once per person.'),
      q('Can I use it on my phone and my laptop?', 'Yes, you can sign in on up to 3 devices, but an account can only be used on one device at a time. Opening it on a second device signs the first one out, so you will never be cut off by accident while playing: you always choose to take over.'),
      q('Does it work offline?', 'You need a connection to sign in and load the instrument. Once it has loaded, the sounds are already in your browser while the page stays open.'),
      q('How do I cancel?', 'Account > Manage billing. You keep access until the end of the period you have paid for.'),
      q('Is the community moderated?', 'Yes. Members can report anything, admins review reports, and you can block anyone. Be kind and keep it about the music and ministry.')];
  }

  /* ---------- pricing ---------- */
  function pricingCard(inLanding) {
    const member = S.session && A.isMember();
    const used = S.profile && S.profile.trial_used;
    let action;
    if (member) action = h('a', { class: 'btn primary big', style: 'width:100%', href: '#/play' }, 'Open the instrument');
    else if (S.session) action = h('button', { class: 'btn primary big', style: 'width:100%', onclick: startCheckout }, used ? 'Subscribe for ' + C.price + ' a month' : 'Start your free month');
    else action = h('a', { class: 'btn primary big', style: 'width:100%', href: '#/signup' }, 'Start your free month');
    return h('section', { class: 'section', id: 'pricing' }, h('div', { class: 'card glow mid center' }, h('p', { class: 'pill', style: 'display:inline-block' }, 'One simple plan'), h('div', { class: 'price' }, C.price, h('small', null, ' / month')), h('p', { class: 'muted' }, 'First month free. Cancel any time.'),
      h('ul', { class: 'ticks', style: 'text-align:left;display:inline-block' }, ['All 8 sounds, and every new one we add', 'Set lists and the Perform screen', 'The member hub: posts, questions, friends and messages', 'Use it on up to 3 of your devices (one at a time)'].map(t => h('li', null, t))),
      action, h('p', { class: 'small muted', style: 'margin:12px 0 0' }, 'A card is needed to start the free month. You will not be charged until it ends.')));
  }
  async function startCheckout() {
    try {
      const r = await api.startCheckout();
      if (r.url) { location.href = r.url; return; }
      mockCheckout(r.trial);
    } catch (e) { A.fail(e); }
  }
  function mockCheckout(trial) {
    A.modal(h('div', null, h('p', { class: 'pill', style: 'display:inline-block' }, 'Demo checkout'), h('h2', null, trial ? 'Start your free month' : 'Subscribe'),
      h('p', { class: 'muted' }, trial ? 'Nothing is charged today. After 30 days it becomes ' + C.price + ' a month unless you cancel.' : C.price + ' a month, starting today.'),
      h('div', { class: 'msg warn small' }, 'This is a pretend checkout. In the live site this page is Stripe\'s, with a real card form.'),
      h('div', { class: 'row' }, h('button', { class: 'btn primary', onclick: async () => { await api.mockCompleteCheckout(); A.closeModal(); await A.refreshState(); A.go('#/account?checkout=success'); } }, trial ? 'Start free month' : 'Subscribe'), h('button', { class: 'btn', onclick: A.closeModal }, 'Cancel'))));
  }
  R.pricing = async () => {
    A.setTitle('Pricing');
    const note = A.query.get('checkout') === 'cancelled' ? h('div', { class: 'msg warn' }, 'Checkout cancelled. You have not been charged.') : null;
    A.render(note, h('div', { class: 'center' }, h('h1', { style: 'font-size:clamp(28px,5vw,40px)' }, 'Simple pricing')), pricingCard(false), h('section', { class: 'mid' }, faq()));
  };

  /* ---------- sign up / log in ---------- */
  R.signup = async () => {
    A.setTitle('Create your account');
    const holder = h('div', { class: 'card narrow' });
    const f = A.form([
      { name: 'username', label: 'Username (letters, numbers, underscore)', max: 20, auto: 'username' },
      { name: 'email', label: 'Email', type: 'email', auto: 'email' },
      { name: 'password', label: 'Password (8+ characters)', type: 'password', auto: 'new-password' },
    ], 'Create account', async v => {
      if (!document.getElementById('agree').checked) throw new Error('Please tick the box to agree to the terms.');
      const r = await api.signUp(v);
      if (r.needsConfirm) { holder.replaceChildren(h('h2', null, 'Check your email'), h('p', { class: 'muted' }, 'We sent a link to ' + v.email + '. Click it to confirm your account, then come back and log in.'), h('a', { class: 'btn', href: '#/login' }, 'Go to log in')); return; }
      await A.afterAuth();
    }, h('div', { class: 'field row', style: 'flex-wrap:nowrap;align-items:flex-start' }, h('input', { id: 'agree', type: 'checkbox', style: 'width:20px;min-height:20px;margin-top:3px' }), h('label', { for: 'agree', style: 'margin:0' }, 'I am 16 or over and agree to the ', h('a', { href: '#/terms', target: '_blank' }, 'Terms'), ', ', h('a', { href: '#/privacy', target: '_blank' }, 'Privacy policy'), ' and ', h('a', { href: '#/rules', target: '_blank' }, 'Community rules'), '.')));
    holder.append(h('h2', null, 'Start your free month'), h('p', { class: 'muted' }, 'Create your account first. You add a card on the next step, and you are not charged for 30 days.'), f, h('p', { class: 'small muted center', style: 'margin:16px 0 0' }, 'Already a member? ', h('a', { href: '#/login' }, 'Log in')));
    A.render(holder);
  };
  R.login = async () => {
    A.setTitle('Log in');
    const f = A.form([{ name: 'email', label: 'Email', type: 'email', auto: 'email' }, { name: 'password', label: 'Password', type: 'password', auto: 'current-password' }], 'Log in', async v => { await api.signIn(v.email, v.password); await A.afterAuth(); });
    A.render(h('div', { class: 'card narrow' }, h('h2', null, 'Welcome back'), f, h('p', { class: 'small center', style: 'margin:16px 0 4px' }, h('a', { href: '#/forgot' }, 'Forgot your password?')), h('p', { class: 'small muted center', style: 'margin:0' }, 'New here? ', h('a', { href: '#/signup' }, 'Start your free month'))));
  };
  R.forgot = async () => {
    A.setTitle('Reset password');
    const f = A.form([{ name: 'email', label: 'Email', type: 'email', auto: 'email' }], 'Send reset link', async (v, msg) => { await api.resetPassword(v.email); msg.replaceChildren(h('div', { class: 'msg ok' }, 'If that email has an account, a reset link is on its way.')); });
    A.render(h('div', { class: 'card narrow' }, h('h2', null, 'Reset your password'), f, h('p', { class: 'small center', style: 'margin:16px 0 0' }, h('a', { href: '#/login' }, 'Back to log in'))));
  };
  R._recovery = () => {
    A.modal(h('div', null, h('h2', null, 'Choose a new password'), A.form([{ name: 'pw', label: 'New password (8+ characters)', type: 'password', auto: 'new-password' }], 'Save password', async v => {
      if (v.pw.length < 8) throw new Error('Password must be at least 8 characters');
      await api.updatePassword(v.pw); A.closeModal(); A.toast('Password updated'); })));
  };

  /* ---------- account ---------- */
  R.account = async () => {
    A.setTitle('Account');
    const [sub, devices, prof] = await Promise.all([api.getSubscription(), api.myDevices(), api.me()]);
    S.sub = sub; S.profile = prof;
    const ok = A.query.get('checkout') === 'success';
    const status = !sub ? h('p', { class: 'muted' }, 'You do not have a membership yet.') : h('dl', { class: 'kv' },
      h('dt', null, 'Status'), h('dd', null, ({ trialing: 'Free month (trial)', active: 'Active', past_due: 'Payment failed', canceled: 'Cancelled', incomplete: 'Not completed' })[sub.status] || sub.status),
      sub.status === 'trialing' && sub.trial_end ? [h('dt', null, 'Free until'), h('dd', null, A.date(sub.trial_end) + ' (then ' + C.price + ' a month)')] : null,
      sub.status === 'active' && sub.current_period_end ? [h('dt', null, sub.cancel_at_period_end ? 'Ends on' : 'Renews on'), h('dd', null, A.date(sub.current_period_end))] : null,
      sub.cancel_at_period_end ? [h('dt', null, 'Cancelling'), h('dd', null, 'You keep access until ' + A.date(sub.current_period_end || sub.trial_end) + '.')] : null);
    const active = sub && ['trialing', 'active'].includes(sub.status);
    const billingBtn = sub ? h('button', { class: 'btn', onclick: openPortal }, 'Manage billing') : null;
    const startBtn = !active ? h('button', { class: 'btn primary', onclick: startCheckout }, prof.trial_used ? 'Subscribe for ' + C.price + ' a month' : 'Start your free month') : null;
    A.render(h('h1', { style: 'font-size:32px' }, 'Your account'), ok ? h('div', { class: 'msg ok' }, 'You are in! Welcome to the hub.') : null,
      h('div', { class: 'grid g2' },
        h('div', { class: 'card' }, h('h3', null, 'Profile'), h('div', { class: 'row', style: 'margin-bottom:12px' }, A.avatar(prof, 'lg'), h('div', null, h('b', null, prof.display_name || prof.username), h('div', { class: 'muted small' }, '@' + prof.username))), h('a', { class: 'btn small', href: '#/u/' + prof.username }, 'Edit profile')),
        h('div', { class: 'card' }, h('h3', null, 'Membership'), status, h('div', { class: 'row' }, startBtn, billingBtn)),
        h('div', { class: 'card' }, h('h3', null, 'Your devices'), h('p', { class: 'small muted' }, 'Up to 3 devices, one in use at a time. Signing in on another device signs this one out.'),
          devices.map(d => h('div', { class: 'list-row' }, h('div', { class: 'grow' }, d.label || 'Device', d.current ? h('span', { class: 'pill', style: 'margin-left:8px' }, 'This device') : null), h('span', { class: 'small muted' }, A.ago(d.last_seen)))), h('details', { style: 'margin-top:12px' }, h('summary', null, 'I changed phone or computer'), A.resetForm())),
        h('div', { class: 'card' }, h('h3', null, 'Your data'), h('p', { class: 'small muted' }, 'Delete your account and everything on it: your profile, posts, comments and messages. Your subscription is cancelled first.'),
          h('div', { class: 'row' }, h('button', { class: 'btn', onclick: A.signOut }, 'Sign out'), h('button', { class: 'btn danger', onclick: deleteAccount }, 'Delete account')))));
  };
  async function openPortal() {
    try { const r = await api.openPortal(); if (r.url) { location.href = r.url; return; } mockPortal(); } catch (e) { A.fail(e); }
  }
  function mockPortal() {
    const s = S.sub || {}; const act = async a => { await api.mockPortalAction(a); A.closeModal(); await A.refreshState(); A.route(); };
    A.modal(h('div', null, h('p', { class: 'pill', style: 'display:inline-block' }, 'Demo billing page'), h('h2', null, 'Manage billing'), h('div', { class: 'msg warn small' }, 'Pretend page. Live, this is Stripe\'s customer portal (cards, invoices, cancel).'),
      h('div', { class: 'grid' }, s.cancel_at_period_end ? h('button', { class: 'btn', onclick: () => act('resume') }, 'Resume membership') : h('button', { class: 'btn', onclick: () => act('cancel') }, 'Cancel at end of period'),
        h('hr', { style: 'margin:6px 0' }), h('p', { class: 'small muted', style: 'margin:0' }, 'Demo shortcuts to test what happens next:'), h('button', { class: 'btn small', onclick: () => act('pastdue') }, 'Make payment fail'), h('button', { class: 'btn small', onclick: () => act('end') }, 'End membership now'), h('button', { class: 'btn small', onclick: () => act('restore') }, 'Restore membership'), h('button', { class: 'btn small', onclick: A.closeModal }, 'Close'))));
  }
  function deleteAccount() {
    A.modal(h('div', null, h('h2', null, 'Delete your account?'), h('p', { class: 'muted' }, 'This removes everything and cannot be undone. Your membership is cancelled so you are not charged again.'), A.form([{ name: 'c', label: 'Type DELETE to confirm' }], 'Delete my account', async v => {
      if (v.c !== 'DELETE') throw new Error('Type DELETE (in capitals) to confirm'); await api.deleteAccount(); A.closeModal(); S.session = S.profile = S.sub = null; A.toast('Your account has been deleted'); A.go('#/'); A.route(); })));
  }

  /* ---------- legal (TEMPLATES: have a solicitor check these before launch) ---------- */
  const todo = t => h('span', { class: 'todo' }, '[' + t + ']');
  const legal = (title, kids) => { A.setTitle(title); A.render(h('div', { class: 'mid legal' }, h('h1', { style: 'font-size:34px' }, title), h('div', { class: 'msg warn small' }, 'Template text. Fill in the highlighted parts and have a solicitor check it before you take real members.'), kids)); };
  R.terms = async () => legal('Terms of use', [
    h('p', { class: 'muted' }, 'Last updated: ', todo('date')),
    h('h2', null, '1. Who we are'), h('p', null, C.siteName + ' is run by ', todo('your name or company'), ', ', todo('address'), '. Contact: ', todo('email'), '.'),
    h('h2', null, '2. Your account'), h('ul', null, h('li', null, 'You must be 16 or over.'), h('li', null, 'One person per account. Do not share your login.'), h('li', null, 'An account can be used on one device at a time and linked to up to 3 devices. A device can be linked to one account only.'), h('li', null, 'Keep your password safe. You are responsible for what happens on your account.')),
    h('h2', null, '3. Membership and payment'), h('ul', null, h('li', null, 'The first month is free (once per person). After that the price is ' + C.price + ' per month, charged automatically to your card until you cancel.'), h('li', null, 'We will not charge you during the free month. Cancel before it ends and you pay nothing.'), h('li', null, 'Cancel any time from Account > Manage billing. You keep access until the end of the period you have paid for.'), h('li', null, todo('refund and cancellation wording, including your rights under UK consumer law'))),
    h('h2', null, '4. Using the instrument'), h('p', null, 'The sounds and software are licensed to you for your own playing, including live performance and recording your own music. You may not copy, extract, resell or redistribute the sounds or the software.'),
    h('h2', null, '5. Community'), h('p', null, 'Posts, comments and messages must follow the ', h('a', { href: '#/rules' }, 'Community rules'), '. We may remove content and suspend accounts that break them.'),
    h('h2', null, '6. Our responsibility'), h('p', null, todo('limitation of liability and availability wording')),
    h('h2', null, '7. Changes and law'), h('p', null, 'We may update these terms and will tell members about important changes. These terms are governed by the law of ', todo('England and Wales'), '.')]);
  R.privacy = async () => legal('Privacy policy', [
    h('p', { class: 'muted' }, 'Last updated: ', todo('date')),
    h('h2', null, 'Who is responsible'), h('p', null, todo('your name or company'), ' is the data controller. Contact: ', todo('email'), '.'),
    h('h2', null, 'What we collect and why'), h('ul', null, h('li', null, 'Account details (email, username, password stored as a secure hash) to run your account.'), h('li', null, 'Device details: a random device ID and a fingerprint of your device\'s hardware and browser, to enforce the one-device rules and prevent repeat free trials.'), h('li', null, 'Payment details are handled by Stripe. We never see or store your card number. We keep your Stripe customer ID and subscription status.'), h('li', null, 'Content you post: profile, posts, comments, friends and messages, to run the community.')),
    h('h2', null, 'Who we share it with'), h('p', null, 'Our providers: Supabase (database and sign-in), Stripe (payments), ', todo('your web host'), '. We do not sell your data.'),
    h('h2', null, 'Your rights'), h('p', null, 'You can see, correct and delete your data. Delete your account from the Account page, or contact us. You can also complain to the Information Commissioner\'s Office (ico.org.uk).'),
    h('h2', null, 'How long we keep it'), h('p', null, 'While your account exists. When you delete your account your profile, posts, comments and messages are removed. ', todo('retention of billing records for tax purposes')) ]);
  R.rules = async () => legal('Community rules', [
    h('p', { class: 'muted' }, 'The hub is for church musicians to learn from each other. Keep it welcoming.'),
    h('ul', null, ['Be kind and respectful. No harassment, hate or personal attacks.', 'Keep it about the music and the ministry. No spam, adverts or selling.', 'Do not share other people\'s private information.', 'No sexual or violent content, and nothing illegal.', 'Do not share or sell the instrument\'s sounds or your login.', 'Use Report if something is wrong. Admins review reports and can remove content or accounts.', 'Use Block to stop someone contacting you.'].map(t => h('li', null, t))),
    h('p', { class: 'muted' }, 'Break the rules and your posts may be removed or your account suspended.')]);
})();
