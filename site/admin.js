// The admin page. The session is an HttpOnly cookie the server sets; this
// script never sees it. Server text goes into the page only as text.
'use strict';

const $ = (id) => document.getElementById(id);
let timer = null;

async function call(method, path, body) {
  const options = { method, cache: 'no-store', headers: {} };
  if (body !== undefined) {
    options.headers['Content-Type'] = 'application/json';
    options.body = JSON.stringify(body);
  }
  const r = await fetch('/api/admin/' + path, options);
  let data = {};
  try { data = await r.json(); } catch (e) { /* empty body */ }
  return { status: r.status, data };
}

// the header, as on the public pages: server name and players online
async function loadHeader() {
  let s = null;
  try {
    const r = await fetch('/api/status', { cache: 'no-store' });
    if (r.ok) s = await r.json();
  } catch (e) { /* offline */ }
  if (s) {
    $('server-name').textContent = s.name;
    document.title = 'Admin · ' + s.name;
  }
  const dot = document.createElement('span');
  dot.className = 'dot';
  dot.setAttribute('aria-hidden', 'true');
  $('online').classList.toggle('down', !s);
  $('online').replaceChildren(dot, s ? s.players + (s.players === 1 ? ' player online' : ' players online') : 'Offline');
}

function show(loggedIn) {
  $('login').hidden = loggedIn;
  $('panel').hidden = !loggedIn;
  clearInterval(timer);
  if (loggedIn) {
    loadPlayers();
    loadLog();
    loadConfig();
    timer = setInterval(loadLog, 10000);
  } else {
    $('password').focus();
  }
}

// SQLite writes UTC as "2026-11-26 17:45:47"
const when = (s) => (s ? new Date(s.replace(' ', 'T') + 'Z').toLocaleString() : 'never');

let players = [];
let open = null;      // the account whose row is open
let mode = 'actions'; // what that row shows: actions, password or delete

async function loadPlayers() {
  const { status, data } = await call('GET', 'accounts');
  if (status === 401) return show(false);
  players = data.accounts || [];
  renderPlayers();
}

function cell(text, className) {
  const td = document.createElement('td');
  td.textContent = text;
  if (className) td.className = className;
  return td;
}

function button(text, className, onClick, type) {
  const b = document.createElement('button');
  b.type = type || 'button';
  if (className) b.className = className;
  b.textContent = text;
  if (onClick) b.addEventListener('click', onClick);
  return b;
}

function setMode(m) {
  mode = m;
  renderPlayers();
  // the field to type in; Cancel when deleting, so Enter alone deletes nobody
  const target = document.querySelector(m === 'password' ? 'tr.detail input' : m === 'delete' ? 'tr.detail .cancel' : 'tr.detail button');
  if (target) target.focus();
}

function detailRow(a) {
  const form = document.createElement('form');
  form.className = 'detail-form';
  const buttons = document.createElement('div');
  buttons.className = 'buttons';
  const out = document.createElement('p');
  out.setAttribute('role', 'status');
  const cancel = button('Cancel', 'secondary cancel', () => setMode('actions'));

  if (mode === 'password') {
    const label = document.createElement('label');
    label.htmlFor = 'pw-' + a.account;
    label.textContent = 'New password for ' + a.account;
    const input = document.createElement('input');
    input.id = 'pw-' + a.account;
    input.pattern = '[A-Za-z0-9]{4,16}';
    input.title = '4-16 letters and digits';
    input.autocomplete = 'new-password';
    buttons.append(button('Set', '', null, 'submit'),
      button('Generate', 'secondary', () => { input.value = ''; save('password', a.account, input, out, 'New password set for'); }),
      cancel);
    form.append(label, input, buttons);
    form.addEventListener('submit', (e) => { e.preventDefault(); save('password', a.account, input, out, 'New password set for'); });
  } else if (mode === 'delete') {
    const question = document.createElement('p');
    question.className = 'question';
    const what = a.personas.length ? ', its personas ' + a.personas.join(', ') + ' and their stats and track records' : '';
    question.textContent = 'Delete ' + a.account + what + '? This cannot be undone. If the player is online, they are disconnected.';
    buttons.append(button('Delete', 'danger', null, 'submit'), cancel);
    form.append(question, buttons);
    form.addEventListener('submit', (e) => { e.preventDefault(); removePlayer(a.account); });
  } else {
    // the dates again: phones don't show their columns
    const meta = document.createElement('p');
    meta.className = 'meta';
    meta.textContent = 'Created ' + when(a.created) + ' · last login ' + when(a.last_login);
    buttons.append(button('Change password', '', () => setMode('password')),
      button('Delete player', 'danger', () => setMode('delete')));
    form.append(meta, buttons);
  }

  const td = document.createElement('td');
  td.colSpan = 4;
  td.append(form, out);
  const tr = document.createElement('tr');
  tr.className = 'detail';
  tr.append(td);
  return tr;
}

function renderPlayers() {
  const q = $('filter').value.trim().toLowerCase();
  const match = (a) => !q || a.account.toLowerCase().includes(q) || a.personas.some((p) => p.toLowerCase().includes(q));
  const list = players.filter(match);
  $('players-count').textContent = players.length + (players.length === 1 ? ' account' : ' accounts') + ' on this server'
    + (q ? ', ' + list.length + ' shown.' : '.');
  const rows = [];
  for (const a of list) {
    // the name stays a button for the keyboard; its click reaches the row like any other
    const name = button(a.account, 'player');
    name.setAttribute('aria-expanded', String(open === a.account));
    const first = document.createElement('td');
    first.append(name);
    const tr = document.createElement('tr');
    tr.className = 'player-row' + (open === a.account ? ' open' : '');
    tr.addEventListener('click', () => {
      open = open === a.account ? null : a.account;
      setMode('actions');
    });
    tr.append(first, cell(a.personas.join(', ') || '–'), cell(when(a.created), 'wide'), cell(when(a.last_login), 'wide'));
    rows.push(tr);
    if (open === a.account) rows.push(detailRow(a));
  }
  if (!list.length) {
    const td = cell(players.length ? 'No account or persona matches.' : 'No accounts yet.');
    td.colSpan = 4;
    td.className = 'empty';
    const tr = document.createElement('tr');
    tr.append(td);
    rows.push(tr);
  }
  $('players').tBodies[0].replaceChildren(...rows);
}

async function removePlayer(account) {
  const { status, data } = await call('POST', 'delete', { account });
  if (status === 401) return show(false);
  const out = $('players-result');
  out.className = status === 200 ? 'ok' : 'error';
  out.textContent = status === 200 ? 'Deleted ' + data.account + '.' : (data.error || 'Failed') + '.';
  open = null;
  loadPlayers();
  loadLog();
}

$('filter').addEventListener('input', renderPlayers);

async function loadLog() {
  const { status, data } = await call('GET', 'log');
  if (status === 401) return show(false);
  const box = $('log');
  const atBottom = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
  box.textContent = (data.lines || []).join('');
  if (atBottom) box.scrollTop = box.scrollHeight;
}

async function loadConfig() {
  const { data } = await call('GET', 'config');
  $('config').textContent = data.text || '';
}

$('login-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const { status, data } = await call('POST', 'login', { password: $('password').value, code: $('code').value });
  $('code').value = '';
  if (status === 200) {
    $('password').value = '';
    $('login-error').textContent = '';
    show(true);
  } else if (status === 429) {
    $('login-error').textContent = 'Too many tries from here. Wait ' + data.retry + ' s.';
  } else if (status === 404) {
    $('login-error').textContent = 'The admin login is off on this server.';
  } else {
    $('login-error').textContent = 'Wrong password or code.';
  }
});

// add a player or set a password: an empty password makes the server pick one
async function save(path, account, password, out, done) {
  const { status, data } = await call('POST', path, { account: account.trim(), password: password.value });
  if (status === 401) return show(false);
  out.replaceChildren();
  if (status !== 200) {
    out.className = 'error';
    out.textContent = data.error ? data.error.charAt(0).toUpperCase() + data.error.slice(1) + '.' : 'Failed.';
    return false;
  }
  out.className = 'ok';
  if (data.password) {
    // shown this once: the server keeps only the hash
    const code = document.createElement('code');
    code.className = 'secret';
    code.textContent = data.password;
    out.append(done + ' ' + data.account + '. Password: ', code, ' – give it to the player now, it is not shown again.');
  } else {
    out.textContent = done + ' ' + data.account + '.';
  }
  password.value = '';
  loadLog();
  return true;
}

async function add(password) {
  const account = $('new-account');
  if (await save('accounts', account.value, password, $('add-result'), 'Added')) {
    account.value = '';
    loadPlayers();
  }
}

$('add-form').addEventListener('submit', (e) => { e.preventDefault(); add($('add-password')); });
$('add-generate').addEventListener('click', () => {
  $('add-password').value = '';
  if ($('add-form').reportValidity()) add($('add-password'));
});
$('logout').addEventListener('click', async () => { await call('POST', 'logout', {}); show(false); });

call('GET', 'session').then(({ status }) => show(status === 200));
loadHeader();
setInterval(() => { if (!document.hidden) loadHeader(); }, 10000);
