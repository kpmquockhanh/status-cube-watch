// Pure helpers for the street tile builder: slippy-map maths, road classes,
// cutting a polyline into per-tile runs, and the tile wire format.

export function lonLatToTile(lon, lat, z) {
  const n = 2 ** z;
  const r = (lat * Math.PI) / 180;
  const fx = ((lon + 180) / 360) * n;
  const fy = ((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2) * n;
  const clamp = (v) => Math.min(n - 1, Math.max(0, Math.floor(v)));
  return { x: clamp(fx), y: clamp(fy) };
}

export function tileBounds(x, y, z) {
  const n = 2 ** z;
  const lon = (tx) => (tx / n) * 360 - 180;
  const lat = (ty) => (Math.atan(Math.sinh(Math.PI * (1 - (2 * ty) / n))) * 180) / Math.PI;
  return { west: lon(x), east: lon(x + 1), north: lat(y), south: lat(y + 1) };
}

const CLASSES = new Map([
  ['motorway', 0], ['motorway_link', 0], ['trunk', 0], ['trunk_link', 0],
  ['primary', 0], ['primary_link', 0],
  ['secondary', 1], ['secondary_link', 1], ['tertiary', 1], ['tertiary_link', 1],
  ['unclassified', 2], ['residential', 2], ['living_street', 2], ['road', 2],
  ['service', 3], ['track', 3],
]);

export function roadClass(highway) {
  return CLASSES.has(highway) ? CLASSES.get(highway) : null;
}

// Cut a polyline into one run per tile it touches. Each segment goes to every
// tile in its bounding box; consecutive segments in the same tile are joined.
export function splitIntoTiles(coords, z) {
  const out = new Map();
  const last = new Map(); // key -> index of the last segment added to the open run
  for (let i = 0; i + 1 < coords.length; i++) {
    const p = coords[i];
    const q = coords[i + 1];
    const a = lonLatToTile(Math.min(p[0], q[0]), Math.max(p[1], q[1]), z);
    const b = lonLatToTile(Math.max(p[0], q[0]), Math.min(p[1], q[1]), z);
    for (let x = a.x; x <= b.x; x++) {
      for (let y = a.y; y <= b.y; y++) {
        const key = `${x}/${y}`;
        let runs = out.get(key);
        if (!runs) { runs = []; out.set(key, runs); }
        if (last.get(key) === i - 1 && runs.length) runs[runs.length - 1].push(q);
        else runs.push([p, q]);
        last.set(key, i);
      }
    }
  }
  return out;
}

export function encodeTile(x, y, z, entries) {
  const w = entries.map(({ cls, coords }) => {
    const run = [cls];
    for (const [lon, lat] of coords) run.push(Math.round(lat * 1e6), Math.round(lon * 1e6));
    return run;
  });
  return JSON.stringify({ v: 1, z, x, y, w });
}
