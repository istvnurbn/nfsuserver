// The public pages: everything comes from the server's JSON API under /api.
// Names from the server only ever go into the page as text, never as HTML.
'use strict';

const REFRESH_MS = 10000;
const MODES = ['all', 'circuit', 'sprint', 'drag', 'drift'];
const VIEWS = ['status', 'races', 'leaderboard', 'records', 'connect'];
const state = { view: 'status', leaderboard: 'all', records: 'circuit', status: null };

function el(tag, attrs, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (k === 'class') e.className = v;
    else e.setAttribute(k, v);
  }
  e.append(...children.filter((c) => c != null && c !== false));
  return e;
}

async function api(path) {
  const r = await fetch('/api/' + path, { cache: 'no-store' });
  if (!r.ok) throw new Error(path + ': ' + r.status);
  return r.json();
}

const cap = (s) => s.charAt(0).toUpperCase() + s.slice(1);

function uptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? d + 'd ' + h + 'h' : h ? h + 'h ' + m + 'm' : m + 'm';
}

// lap times in ms, points on the drift tracks
function score(track, value) {
  if (trackMode(track) === 'drift') return value.toLocaleString('en') + ' pts';
  const m = Math.floor(value / 60000), s = ((value % 60000) / 1000).toFixed(3).padStart(6, '0');
  return m + ':' + s;
}

function setOnline(ok) {
  document.getElementById('offline').hidden = ok;
  const online = document.getElementById('online');
  online.classList.toggle('down', !ok);
  const s = state.status;
  online.replaceChildren(el('span', { class: 'dot', 'aria-hidden': 'true' }),
    ok && s ? s.players + (s.players === 1 ? ' player online' : ' players online') : 'Offline');
}

// --- status -------------------------------------------------------------

function tile(value, label) {
  return el('div', { class: 'tile' }, el('div', { class: 'value' }, value), el('div', { class: 'label' }, label));
}

function roomCard(room) {
  const people = room.players.length
    ? el('ul', null, ...room.players.map((p) => el('li', null, p)))
    : el('p', { class: 'empty' }, 'Empty');
  return el('article', { class: 'card' + (room.players.length ? '' : ' empty-room') },
    el('header', null, el('h3', null, cap(room.mode || 'Room') + ' · ' + room.name), el('span', { class: 'count' }, room.players.length)),
    people);
}

function renderStatus() {
  const s = state.status;
  if (!s) return;
  document.getElementById('server-meta').textContent = 'Up ' + uptime(s.uptime) + ' · nfsuserver ' + s.version;
  document.getElementById('stats').replaceChildren(
    tile(s.players, 'Players online'), tile(s.racing, 'Racing now'), tile(s.rooms.length, 'Rooms'));
  const order = (a, b) => MODES.indexOf(a.mode) - MODES.indexOf(b.mode) || a.name.localeCompare(b.name);
  const rooms = s.rooms.slice().sort(order);
  document.getElementById('rooms-ranked').replaceChildren(...rooms.filter((r) => r.ranked).map(roomCard));
  document.getElementById('rooms-unranked').replaceChildren(...rooms.filter((r) => !r.ranked).map(roomCard));
}

async function loadStatus() {
  try {
    state.status = await api('status');
    document.getElementById('server-name').textContent = state.status.name;
    document.title = state.status.name + ' · NFSU lobby server';
    setOnline(true);
  } catch (e) {
    setOnline(false);
  }
  if (state.view === 'status') renderStatus();
  if (state.view === 'connect') renderConnect();
}

// --- races --------------------------------------------------------------

function raceCard(race) {
  const mode = trackMode(race.track) || race.room.mode;
  const badge = race.started
    ? el('span', { class: 'badge racing' }, 'Racing')
    : el('span', { class: 'badge waiting' }, 'Waiting ' + race.players.length + '/' + race.max);
  const details = [cap(mode || ''), race.laps != null && mode !== 'drag' ? race.laps + (race.laps === 1 ? ' lap' : ' laps') : null,
    race.room.ranked ? 'Ranked' : 'Unranked', 'Room ' + race.room.name].filter(Boolean).join(' · ');
  return el('article', { class: 'card' },
    el('header', null, el('h3', null, trackName(race.track)), badge),
    el('p', { class: 'meta' }, details),
    el('ul', null, ...race.players.map((p) => el('li', null, el('span', null, p.persona), el('span', { class: 'car' }, carName(p.car))))));
}

async function loadRaces() {
  const { races } = await api('races');
  races.sort((a, b) => b.started - a.started);
  const box = document.getElementById('races');
  if (races.length) box.replaceChildren(...races.map(raceCard));
  else box.replaceChildren(el('p', { class: 'empty' }, 'No races right now.'));
}

// --- leaderboard and records ------------------------------------------------

function tabs(name, modes, onPick) {
  const box = document.querySelector('.tabs[data-for="' + name + '"]');
  box.replaceChildren(...modes.map((m) => {
    const b = el('button', { type: 'button', 'aria-pressed': String(state[name] === m) }, cap(m));
    b.addEventListener('click', () => { state[name] = m; onPick(); });
    return b;
  }));
}

async function loadLeaderboard() {
  tabs('leaderboard', MODES, loadLeaderboard);
  const { players } = await api('leaderboard?mode=' + state.leaderboard);
  const body = document.querySelector('#leaderboard tbody');
  if (!players.length) {
    body.replaceChildren(el('tr', null, el('td', { colspan: '6', class: 'empty' }, 'No players yet.')));
    return;
  }
  body.replaceChildren(...players.map((p) => el('tr', null,
    el('td', null, p.position), el('td', null, p.persona), el('td', { class: 'num' }, p.rep),
    el('td', { class: 'num' }, p.wins), el('td', { class: 'num' }, p.losses), el('td', { class: 'num' }, p.disconnects))));
}

function recordCard(t) {
  const rows = t.records.map((r, i) => el('tr', null,
    el('td', null, i + 1), el('td', null, r.persona),
    el('td', { class: 'car' }, carName(r.car)), el('td', { class: 'num' }, score(t.track, r.score))));
  return el('article', { class: 'card' },
    el('header', null, el('h3', null, trackName(t.track))),
    rows.length ? el('table', null, el('tbody', null, ...rows)) : el('p', { class: 'empty' }, 'No times yet.'));
}

async function loadRecords() {
  tabs('records', MODES.slice(1), loadRecords);
  const { tracks } = await api('records');
  document.getElementById('records').replaceChildren(...tracks.filter((t) => trackMode(t.track) === state.records).map(recordCard));
}

// --- connect ------------------------------------------------------------------

function renderConnect() {
  const address = (state.status && state.status.address) || location.hostname;
  // the hosts file takes addresses only, Server Changer takes names too
  const ip = /^\d{1,3}(\.\d{1,3}){3}$/.test(address) ? address : 'SERVER-IP';
  document.getElementById('address').textContent = address;
  document.getElementById('hosts-hint').hidden = ip === address;
  document.getElementById('hosts-line').textContent = ip + ' ps2nfs04.ea.com';
  document.getElementById('nixos-line').textContent = 'networking.hosts."' + ip + '" = [ "ps2nfs04.ea.com" ];';
}

// --- pages ----------------------------------------------------------------------

const LOADERS = { status: renderStatus, races: loadRaces, leaderboard: loadLeaderboard, records: loadRecords, connect: renderConnect };

async function show() {
  const view = VIEWS.includes(location.hash.slice(1)) ? location.hash.slice(1) : 'status';
  state.view = view;
  for (const v of VIEWS) document.getElementById('view-' + v).hidden = v !== view;
  for (const a of document.querySelectorAll('nav a')) {
    if (a.getAttribute('href') === '#' + view) a.setAttribute('aria-current', 'page');
    else a.removeAttribute('aria-current');
  }
  try { await LOADERS[view](); } catch (e) { setOnline(false); }
}

// status (for the header) and races refresh while the page is visible
async function tick() {
  if (document.hidden) return;
  await loadStatus();
  if (state.view === 'races') { try { await loadRaces(); } catch (e) { setOnline(false); } }
}

window.addEventListener('hashchange', show);
document.addEventListener('visibilitychange', tick);
loadStatus().then(show);
setInterval(tick, REFRESH_MS);
