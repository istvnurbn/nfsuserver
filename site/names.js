// Track and car names for the ids the JSON API sends.
'use strict';

const TRACKS = {
  1001: 'Market Street', 1002: 'Stadium', 1003: 'Olympic Square', 1004: 'Terminal',
  1005: 'Atlantica', 1006: 'Inner City', 1007: 'Port Royal', 1008: 'National Rail',
  1102: 'Liberty Gardens', 1103: 'Broadway', 1104: 'Lock Up', 1105: '9th & Frey',
  1106: 'Bedard Bridge', 1107: 'Spillway', 1108: '1st Ave. Truck Stop', 1109: '7th & Sparling',
  1201: '14th & Vine Construction', 1202: 'Highway 1', 1206: 'Main Street',
  1207: 'Commercial', 1210: '14th & Vine', 1214: 'Main Street Construction',
  1301: 'Drift Track 1', 1302: 'Drift Track 2', 1303: 'Drift Track 3', 1304: 'Drift Track 4',
  1305: 'Drift Track 5', 1306: 'Drift Track 6', 1307: 'Drift Track 7', 1308: 'Drift Track 8',
};

// the third character of the game's car text, A = 0 ... T = 19
const CARS = [
  'VW Golf GTI', 'Ford Focus', 'Mazda Miata MX-5', 'Dodge Neon', 'Honda Civic',
  'Peugeot 206', 'Toyota Celica', 'Mitsubishi Eclipse', 'Mazda RX-7', 'Toyota Supra',
  'Honda S2000', 'Acura RSX', 'Subaru Impreza', 'Mitsubishi Lancer', 'Acura Integra Type R',
  'Hyundai Tiburon GT', 'Nissan 350Z', 'Nissan Sentra SE-R', 'Nissan 240SX', 'Nissan Skyline GT-R',
];

// 10xx circuit, 11xx sprint, 12xx drag, 13xx drift
const trackMode = (id) => ['circuit', 'sprint', 'drag', 'drift'][Math.floor(id / 100) - 10];
const trackName = (id) => TRACKS[id] || (id == null ? 'Unknown track' : 'Track ' + id);
const carName = (id) => (id == null ? '' : CARS[id] || 'Car ' + id);
