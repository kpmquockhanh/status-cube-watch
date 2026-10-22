import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  lonLatToTile, tileBounds, roadClass, splitIntoTiles, encodeTile,
} from './tiles.mjs';

const Z = 15;

function slippy(lon, lat, z) {
  const n = 2 ** z;
  const x = Math.floor(((lon + 180) / 360) * n);
  const r = (lat * Math.PI) / 180;
  const y = Math.floor(((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2) * n);
  return { x, y };
}

function centre(x, y) {
  const b = tileBounds(x, y, Z);
  return [(b.west + b.east) / 2, (b.south + b.north) / 2];
}

test('lonLatToTile matches the slippy formula (Ben Thanh market)', () => {
  const t = lonLatToTile(106.6980, 10.7725, Z);
  assert.deepEqual(t, slippy(106.6980, 10.7725, Z));
  assert.deepEqual(t, { x: 26095, y: 15397 });
});

test('tileBounds contains the tile centre and round-trips', () => {
  const [lon, lat] = centre(26095, 15397);
  assert.deepEqual(lonLatToTile(lon, lat, Z), { x: 26095, y: 15397 });
  const b = tileBounds(26095, 15397, Z);
  assert.ok(b.west < b.east && b.south < b.north);
});

test('roadClass', () => {
  assert.equal(roadClass('trunk_link'), 0);
  assert.equal(roadClass('secondary'), 1);
  assert.equal(roadClass('living_street'), 2);
  assert.equal(roadClass('service'), 3);
  assert.equal(roadClass('footway'), null);
  assert.equal(roadClass('motorway'), 0);
  assert.equal(roadClass('primary'), 1);
  assert.equal(roadClass('primary_link'), 1);
  assert.equal(roadClass('tertiary'), 2);
  assert.equal(roadClass('tertiary_link'), 2);
  assert.equal(roadClass('unclassified'), 2);
  assert.equal(roadClass('road'), null);
  assert.equal(roadClass('track'), null);
});

test('a line across a tile seam appears in both tiles', () => {
  const a = centre(26095, 15397);
  const b = centre(26096, 15397);
  const m = splitIntoTiles([a, b], Z);
  for (const key of ['26095/15397', '26096/15397']) {
    assert.ok(m.has(key), key);
    const runs = m.get(key);
    assert.equal(runs.length, 1);
    assert.deepEqual(runs[0], [a, b]);
  }
});

test('a line inside one tile is one run', () => {
  const [lon, lat] = centre(26095, 15397);
  const pts = [0, 1, 2, 3, 4].map((i) => [lon + i * 1e-4, lat + i * 1e-4]);
  const m = splitIntoTiles(pts, Z);
  assert.equal(m.size, 1);
  assert.equal(m.get('26095/15397').length, 1);
  assert.equal(m.get('26095/15397')[0].length, 5);
});

test('encodeTile is latitude first, 1e6 integers', () => {
  const s = encodeTile(26095, 15397, Z, [{ cls: 1, coords: [[106.698, 10.7725], [106.6985, 10.773]] }]);
  assert.deepEqual(JSON.parse(s), {
    v: 1, z: 15, x: 26095, y: 15397,
    w: [[1, 10772500, 106698000, 10773000, 106698500]],
  });
});
