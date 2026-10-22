// osmium export -f geojsonseq ... | node build-tiles.mjs <outDir>
// Writes <outDir>/15/<x>/<y>.json and a gzip twin <y>.json.gz.
// Streams stdin; when too many points are held it spills per-tile part files.

import { createInterface } from 'node:readline';
import { appendFileSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { gzipSync } from 'node:zlib';
import { encodeTile, roadClass, splitIntoTiles } from './tiles.mjs';

const Z = 15;
const FLUSH_POINTS = Number(process.env.TILES_FLUSH_POINTS) || 2_000_000;

const outDir = process.argv[2];
if (!outDir) {
  console.error('usage: osmium export ... -f geojsonseq | node build-tiles.mjs <outDir>');
  process.exit(2);
}
const partsDir = path.join(outDir, '.parts');
rmSync(partsDir, { recursive: true, force: true });

let mem = new Map(); // "x/y" -> [{cls, coords}]
let points = 0;
let ways = 0;
const spilled = new Set();

function flush() {
  for (const [key, entries] of mem) {
    const file = path.join(partsDir, `${key}.jsonl`);
    mkdirSync(path.dirname(file), { recursive: true });
    appendFileSync(file, entries.map((e) => JSON.stringify(e)).join('\n') + '\n');
    spilled.add(key);
  }
  mem = new Map();
  points = 0;
}

function addLine(cls, coords) {
  if (coords.length < 2) return;
  for (const [key, runs] of splitIntoTiles(coords, Z)) {
    let list = mem.get(key);
    if (!list) { list = []; mem.set(key, list); }
    for (const run of runs) { list.push({ cls, coords: run }); points += run.length; }
  }
  if (points > FLUSH_POINTS) flush();
}

const rl = createInterface({ input: process.stdin, crlfDelay: Infinity });
for await (const raw of rl) {
  const line = raw.replace(/^\x1e/, '').trim(); // geojsonseq record separator
  if (!line) continue;
  let f;
  try { f = JSON.parse(line); } catch { continue; }
  const g = f?.geometry;
  if (!g || (g.type !== 'LineString' && g.type !== 'MultiLineString')) continue;
  const cls = roadClass(f.properties?.highway);
  if (cls === null) continue;
  ways++;
  if (g.type === 'LineString') addLine(cls, g.coordinates);
  else for (const part of g.coordinates) addLine(cls, part);
}

const keys = new Set([...mem.keys(), ...spilled]);
let tiles = 0;
for (const key of keys) {
  let entries = [];
  if (spilled.has(key)) {
    const text = readFileSync(path.join(partsDir, `${key}.jsonl`), 'utf8');
    entries = text.split('\n').filter(Boolean).map((l) => JSON.parse(l));
  }
  if (mem.has(key)) entries = entries.concat(mem.get(key));
  const [x, y] = key.split('/').map(Number);
  const json = encodeTile(x, y, Z, entries);
  const base = path.join(outDir, String(Z), String(x));
  mkdirSync(base, { recursive: true });
  writeFileSync(path.join(base, `${y}.json`), json);
  writeFileSync(path.join(base, `${y}.json.gz`), gzipSync(json));
  tiles++;
}
rmSync(partsDir, { recursive: true, force: true });
console.log(`tiles: ${tiles}, ways: ${ways}`);
