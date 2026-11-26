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

function show(loggedIn) {
  $('login').hidden = loggedIn;
  $('panel').hidden = !loggedIn;
  $('logout').hidden = !loggedIn;
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
let open = null; // the account whose password form is showing

async function loadPlayers() {
  const { status, data } = await call('GET', 'accounts');
  if (status === 401) return show(false);
  players = data.accounts || [];
  renderPlayers();
}

function cell(text) {
  const td = document.createElement('td');
  td.textContent = text;
  return td;
}

function passwordRow(account) {
  const form = document.createElement('form');
  form.className = 'row';
  const id = 'pw-' + account;
  const box = document.createElement('div');
  const label = document.createElement('label');
  label.htmlFor = id;
  label.textContent = 'New password for ' + account;
  const input = document.createElement('input');
  input.id = id;
  input.pattern = '[A-Za-z0-9]{4,16}';
  input.title = '4-16 letters and digits';
  input.autocomplete = 'new-password';
  box.append(label, input);
  const set = document.createElement('button');
  set.type = 'submit';
  set.textContent = 'Set password';
  const generate = document.createElement('button');
  generate.type = 'button';
  generate.className = 'secondary';
  generate.textContent = 'Generate password';
  const out = document.createElement('p');
  out.setAttribute('role', 'status');
  const remove = document.createElement('button');
  remove.type = 'button';
  remove.className = 'danger';
  remove.textContent = 'Delete player';
  remove.addEventListener('click', () => removePlayer(account));
  form.append(box, set, generate, remove);
  form.addEventListener('submit', (e) => { e.preventDefault(); save('password', account, input, out, 'New password set for'); });
  generate.addEventListener('click', () => { input.value = ''; save('password', account, input, out, 'New password set for'); });

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
    const name = document.createElement('button');
    name.type = 'button';
    name.className = 'player';
    name.textContent = a.account;
    name.setAttribute('aria-expanded', String(open === a.account));
    name.addEventListener('click', () => {
      open = open === a.account ? null : a.account;
      renderPlayers();
      if (open) $('pw-' + open).focus();
    });
    const first = document.createElement('td');
    first.append(name);
    const tr = document.createElement('tr');
    tr.append(first, cell(a.personas.join(', ') || '–'), cell(when(a.created)), cell(when(a.last_login)));
    rows.push(tr);
    if (open === a.account) rows.push(passwordRow(a.account));
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
  const a = players.find((p) => p.account === account);
  const what = a && a.personas.length ? ' with the personas ' + a.personas.join(', ') + ', their stats and track records' : '';
  if (!confirm('Delete ' + account + what + '? This cannot be undone. A player online now is disconnected.')) return;
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
