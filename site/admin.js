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
let mode = 'actions'; // what that row shows: actions, password, names, persona, unpersona or delete
let totp = true;      // false in LAN mode: the password alone

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
  const target = document.querySelector(m === 'password' || m === 'names' || m === 'persona' ? 'tr.detail input' : m === 'delete' || m === 'unpersona' ? 'tr.detail .cancel' : 'tr.detail button');
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
  } else if (mode === 'persona') {
    const label = document.createElement('label');
    label.htmlFor = 'new-persona-' + a.account;
    label.textContent = 'New persona for ' + a.account;
    const input = document.createElement('input');
    input.id = 'new-persona-' + a.account;
    input.pattern = '[A-Za-z0-9]{3,12}';
    input.title = '3-12 letters and digits';
    input.required = true;
    input.autocomplete = 'off';
    buttons.append(button('Add', '', null, 'submit'), cancel);
    form.append(label, input, buttons);
    form.addEventListener('submit', (e) => { e.preventDefault(); addPersona(a.account, input.value.trim(), out); });
  } else if (mode === 'unpersona') {
    const question = document.createElement('p');
    question.className = 'question';
    question.textContent = 'Delete which persona of ' + a.account + '? Its stats and track records go with it. This cannot be undone. If the player is online, they are disconnected.';
    for (const p of a.personas) buttons.append(button('Delete ' + p, 'danger', () => removePersona(a.account, p, out)));
    buttons.append(cancel);
    form.append(question, buttons);
  } else if (mode === 'names') {
    const field = (id, text, value, pattern, title) => {
      const label = document.createElement('label');
      label.htmlFor = id;
      label.textContent = text;
      const input = document.createElement('input');
      input.id = id;
      input.value = value;
      input.pattern = pattern;
      input.title = title;
      input.required = true;
      input.autocomplete = 'off';
      form.append(label, input);
      return input;
    };
    const account = field('name-' + a.account, 'Account', a.account, '[A-Za-z0-9]{4,16}', '4-16 letters and digits');
    const personas = a.personas.map((p, i) => field('persona-' + i + '-' + a.account, 'Persona ' + (i + 1), p, '[A-Za-z0-9]{3,12}', '3-12 letters and digits'));
    const note = document.createElement('p');
    note.className = 'question';
    note.textContent = 'Stats and track records move with a renamed persona. If the player is online, they are disconnected.';
    buttons.append(button('Save', '', null, 'submit'), cancel);
    form.append(note, buttons);
    form.addEventListener('submit', (e) => { e.preventDefault(); rename(a, account, personas, out); });
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
      button('Edit names', 'secondary', () => setMode('names')));
    if (a.personas.length < 4) buttons.append(button('Add persona', 'secondary', () => setMode('persona'))); // the game's limit
    if (a.personas.length) buttons.append(button('Delete persona', 'danger', () => setMode('unpersona')));
    buttons.append(button('Delete player', 'danger', () => setMode('delete')));
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

async function removePersona(account, persona, out) {
  const { status, data } = await call('POST', 'deletepersona', { account, persona });
  if (status === 401) return show(false);
  if (status !== 200) {
    out.className = 'error';
    out.textContent = data.error ? data.error.charAt(0).toUpperCase() + data.error.slice(1) + '.' : 'Failed.';
    return;
  }
  const result = $('players-result');
  result.className = 'ok';
  result.textContent = 'Deleted persona ' + data.persona + ' of ' + account + '.';
  mode = 'actions';
  loadPlayers();
  loadLog();
}

async function addPersona(account, persona, out) {
  const { status, data } = await call('POST', 'personas', { account, persona });
  if (status === 401) return show(false);
  if (status !== 200) {
    out.className = 'error';
    out.textContent = data.error ? data.error.charAt(0).toUpperCase() + data.error.slice(1) + '.' : 'Failed.';
    return;
  }
  const result = $('players-result');
  result.className = 'ok';
  result.textContent = 'Added persona ' + data.persona + ' to ' + account + '.';
  mode = 'actions';
  loadPlayers();
  loadLog();
}

// personas first: their requests name the account by its old name
async function rename(a, account, personas, out) {
  const fail = (status, data) => {
    if (status === 401) return show(false);
    out.className = 'error';
    out.textContent = data.error ? data.error.charAt(0).toUpperCase() + data.error.slice(1) + '.' : 'Failed.';
    loadPlayers();
  };
  const changed = [];
  for (let i = 0; i < personas.length; i++) {
    const to = personas[i].value.trim();
    if (to === a.personas[i]) continue;
    const { status, data } = await call('POST', 'persona', { account: a.account, persona: a.personas[i], to });
    if (status !== 200) return fail(status, data);
    changed.push(a.personas[i] + ' → ' + to);
  }
  const to = account.value.trim();
  if (to !== a.account) {
    const { status, data } = await call('POST', 'rename', { account: a.account, to });
    if (status !== 200) return fail(status, data);
    changed.unshift(a.account + ' → ' + to);
    open = to;
  }
  const result = $('players-result');
  result.className = 'ok';
  result.textContent = changed.length ? 'Renamed ' + changed.join(', ') + '.' : 'No names changed.';
  mode = 'actions';
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
  } else if (status === 403) {
    $('login-error').textContent = 'This server takes admin logins only from its local network.';
  } else {
    $('login-error').textContent = totp ? 'Wrong password or code.' : 'Wrong password.';
  }
});

// add a player or set a password: an empty password makes the server pick one
async function save(path, account, password, out, done, extra) {
  const { status, data } = await call('POST', path, Object.assign({ account: account.trim(), password: password.value }, extra));
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
  const persona = $('add-persona');
  if (await save('accounts', account.value, password, $('add-result'), 'Added', { persona: persona.value.trim() })) {
    account.value = '';
    persona.value = '';
    loadPlayers();
  }
}

$('add-form').addEventListener('submit', (e) => { e.preventDefault(); add($('add-password')); });
$('add-generate').addEventListener('click', () => {
  $('add-password').value = '';
  if ($('add-form').reportValidity()) add($('add-password'));
});
$('logout').addEventListener('click', async () => { await call('POST', 'logout', {}); show(false); });

call('GET', 'session').then(({ status, data }) => {
  // LAN mode: no code field
  totp = data.totp !== false;
  $('code-field').hidden = !totp;
  $('code').required = totp;
  show(status === 200);
});
loadHeader();
setInterval(() => { if (!document.hidden) loadHeader(); }, 10000);
