// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8,
// backend/internal/web/static/app.js (the "sprite" section: the critter, its
// poses and the hat art), unchanged but for the exports and the think pose's
// animation. The firmware draws the same art (critter.cpp, hat_art.cpp), and the
// Mac menu a port of those (mac-helper/.../Critter.swift); edit
// them together.

// ---------------------------------------------------------------- sprite
// Pixel critter (viewBox 0 -4 18 13; mirrors critter.dart on the panel): Claude's flat
// 12×6 block with 1×2 arms, on short legs, wearing its working folder's hat. Colours
// come from CSS: .b = var(--body), .c / .cx = var(--cut) (the background behind it).
const BODY = 'M1 0h12v6h-12z';
const LEGS_A = [2, 4, 9, 11];
const LEGS_B = [3, 5, 8, 10];
const critterLegs = (xs, cls = '') => xs.map((x) => `<rect class="b ${cls}" x="${x}" y="6" width="1" height="1.6"/>`).join('');
// Codex onigiri: a rounded rice triangle wrapped in nori, on two little feet that
// step in turn (legs B lifts the left foot).
const botFeet = (xs, cls = '') => `<rect class="b ${cls}" x="3.5" y="${xs === LEGS_B ? 6.4 : 7}" width="2" height="1.4"/><rect class="b ${cls}" x="8.5" y="7" width="2" height="1.4"/>`;
const BOT_BODY = 'M6 -3h2v1h1v1h1v1h1v1h1v2h1v2h1v1h-1v1h-12v-1h-1v-1h1v-2h1v-2h1v-1h1v-1h1v-1h1z';
const BOT_NORI = '<rect class="c" x="4" y="5.4" width="6" height="1.6"/>';
let legs = critterLegs;
const eyesOpen = (y) => `<rect class="c" x="4" y="${y}" width="1" height="2"/><rect class="c" x="9" y="${y}" width="1" height="2"/>`;
const eyesOpenAt = (y, h) => `<rect class="c" x="4" y="${y}" width="1" height="${h}"/><rect class="c" x="9" y="${y}" width="1" height="${h}"/>`;
const eyesShut = '<rect class="c" x="3.5" y="3.2" width="2" height="0.55"/><rect class="c" x="8.5" y="3.2" width="2" height="0.55"/>';
const DOTS = '<g class="dots"><rect class="b" x="14.6" y="-2.6" width="0.9" height="0.9"/><rect class="b" x="15.9" y="-2.6" width="0.9" height="0.9"/><rect class="b" x="17.2" y="-2.6" width="0.8" height="0.9"/></g>';
const pixels = (rows, x0, w, cls, h = 0.9, y0 = -4) => `<g class="${cls}">` + rows.flatMap((row, r) => [...row].map((ch, c) =>
  ch === '#' ? `<rect class="b" x="${x0 + c * w}" y="${y0 + r * h}" width="${w}" height="${h}"/>` : '')).join('') + '</g>';

const MINI = (x, y, s) => `<g transform="translate(${x} ${y}) scale(${s})"><g class="bob"><path class="b" d="${BODY}"/>` +
  '<rect class="b" x="0" y="2" width="1" height="2"/><rect class="b" x="13" y="2" width="1" height="2"/>' +
  `<rect class="c" x="4" y="1.5" width="1" height="2"/><rect class="c" x="9" y="1.5" width="1" height="2"/></g>${critterLegs(LEGS_A, 'la')}${critterLegs(LEGS_B, 'lb')}</g>`;

// Hats: bitmap rows placed at (x, top); a = the hat's colour, b = its shade,
// c = highlight, d = near-black; extras are single cells (x, y, key). Every hat
// has a still version and the frames it cycles through while its agent works,
// for the critter and for the onigiri (narrower, sitting over its tip). The
// bandana's knot and tails sit on the left. Mirrors critter.dart; generated
// from the approved design, so edit both together.
const HAT_ART = {
  "hard hat": {
  ms: 400,
  critterStill: { x: 0, top: -4, rows: ["....aaaaa.....", "...acaaaaa....", "..aaaaaaaaa...", ".bbbbbbbbbbbb."], extras: [] },
  critter: [
    { x: 0, top: -4, rows: ["....aaaaa.....", "...acaaaaa....", "..aaaaccaaa...", ".bbbbbbbbbbbb."], extras: [] },
    { x: 0, top: -4, rows: ["....aaaaa.....", "...acaaaaa....", "..aaaabbaaa...", ".bbbbbbbbbbbb."], extras: [] },
  ],
  onigiriStill: { x: 0, top: -3, rows: [".....aaaa.....", "....acaaaa....", "...bbbbbbbb..."], extras: [] },
  onigiri: [
    { x: 0, top: -3, rows: [".....aaaa.....", "....acccaa....", "...bbbbbbbb..."], extras: [] },
    { x: 0, top: -3, rows: [".....aaaa.....", "....acbbaa....", "...bbbbbbbb..."], extras: [] },
  ],
  },
  cap: {
  ms: 300,
  critterStill: { x: -3, top: -3, rows: ["........aaaaaa", ".......aaaaacaa", "bbbbbbbbbbbbbbb"], extras: [] },
  critter: [
    { x: -3, top: -3, rows: ["........aaaaaa", ".......aaaaacaa", "bbbbbbbbbbbbbbb"], extras: [] },
    { x: -3, top: -3, rows: ["........aaaaaa", ".......aaaaacaa", "bbbbbbbbbbbbbbb"], extras: [] },
    { x: -3, top: -3, rows: ["........aaaaaa", ".......aaaaacaa", "...bbbbbbbbbbbb"], extras: [[-3, 0, "b"], [-2, 0, "b"]] },
    { x: -3, top: -3, rows: ["........aaaaaa", ".......aaaaacaa", "bbbbbbbbbbbbbbb"], extras: [] },
  ],
  onigiriStill: { x: -1, top: -3, rows: ["......aaaa", ".....aaacaa", "bbbbbbbbbbbb"], extras: [] },
  onigiri: [
    { x: -1, top: -3, rows: ["......aaaa", ".....aaacaa", "bbbbbbbbbbbb"], extras: [] },
    { x: -1, top: -3, rows: ["......aaaa", ".....aaacaa", "bbbbbbbbbbbb"], extras: [] },
    { x: -1, top: -3, rows: ["......aaaa", ".....aaacaa", "..bbbbbbbbbb"], extras: [[-1, 0, "b"]] },
    { x: -1, top: -3, rows: ["......aaaa", ".....aaacaa", "bbbbbbbbbbbb"], extras: [] },
  ],
  },
  beanie: {
  ms: 350,
  critterStill: { x: 0, top: -4, rows: [".....cc.......", "....aaaaaa....", "...aaaaaaaa...", "..babababab..."], extras: [] },
  critter: [
    { x: 0, top: -4, rows: [".....cc.......", "....aaaaaa....", "...aaaaaaaa...", "..babababab..."], extras: [] },
    { x: 0, top: -3, rows: ["....aaaaaa....", "...aaaaaaaa...", "..babababab..."], extras: [[5, -5, "c"], [6, -5, "c"]] },
  ],
  onigiriStill: { x: 0, top: -4, rows: ["......cc......", ".....aaaa.....", "....aaaaaa....", "...babababa..."], extras: [] },
  onigiri: [
    { x: 0, top: -4, rows: ["......cc......", ".....aaaa.....", "....aaaaaa....", "...babababa..."], extras: [] },
    { x: 0, top: -3, rows: [".....aaaa.....", "....aaaaaa....", "...babababa..."], extras: [[6, -5, "c"], [7, -5, "c"]] },
  ],
  },
  helmet: {
  ms: 280,
  critterStill: { x: 0, top: -4, rows: ["....aaaaaa....", "...aaaaaaaa...", "..aaaacaaaaa..", "..bbbbbbbbbb.."], extras: [] },
  critter: [
    { x: 0, top: -4, rows: ["....aaaaaa....", "...aaaaaaaa...", "..acaaaaaaaa..", "..bbbbbbbbbb.."], extras: [] },
    { x: 0, top: -4, rows: ["....aaaaaa....", "...aaaaaaaa...", "..aaaaacaaaa..", "..bbbbbbbbbb.."], extras: [] },
    { x: 0, top: -4, rows: ["....aaaaaa....", "...aaaaaaaa...", "..aaaaaaaaca..", "..bbbbbbbbbb.."], extras: [] },
    { x: 0, top: -4, rows: ["....aaaaaa....", "...aaaaaaaa...", "..aaaaaaaaaa..", "..bbbbbbbbbb.."], extras: [] },
  ],
  onigiriStill: { x: 0, top: -3, rows: [".....aaaa.....", "....aacaaa....", "...bbbbbbbb..."], extras: [] },
  onigiri: [
    { x: 0, top: -3, rows: [".....aaaa.....", "....acaaaa....", "...bbbbbbbb..."], extras: [] },
    { x: 0, top: -3, rows: [".....aaaa.....", "....aaacaa....", "...bbbbbbbb..."], extras: [] },
    { x: 0, top: -3, rows: [".....aaaa.....", "....aaaaca....", "...bbbbbbbb..."], extras: [] },
    { x: 0, top: -3, rows: [".....aaaa.....", "....aaaaaa....", "...bbbbbbbb..."], extras: [] },
  ],
  },
  wizard: {
  ms: 300,
  critterStill: { x: 0, top: -5, rows: ["........a.....", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."], extras: [] },
  critter: [
    { x: 0, top: -5, rows: ["........a.....", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."], extras: [] },
    { x: 0, top: -5, rows: [".........a....", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."], extras: [[11, -6, "c"]] },
    { x: 0, top: -5, rows: ["........a.....", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."], extras: [] },
    { x: 0, top: -5, rows: [".......a......", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."], extras: [] },
  ],
  onigiriStill: { x: 0, top: -5, rows: ["......aa......", "......aa......", ".....acaa.....", "....aaaaaa....", "...bbbbbbbb..."], extras: [] },
  onigiri: [
    { x: 0, top: -5, rows: ["......a.......", "......aa......", ".....acaa.....", "....aaaaaa....", "...bbbbbbbb..."], extras: [] },
    { x: 0, top: -5, rows: [".......a......", "......aa......", ".....acaa.....", "....aaaaaa....", "...bbbbbbbb..."], extras: [[9, -6, "c"]] },
    { x: 0, top: -5, rows: ["......a.......", "......aa......", ".....acaa.....", "....aaaaaa....", "...bbbbbbbb..."], extras: [] },
    { x: 0, top: -5, rows: [".....a........", "......aa......", ".....acaa.....", "....aaaaaa....", "...bbbbbbbb..."], extras: [] },
  ],
  },
  beret: {
  ms: 300,
  critterStill: { x: 0, top: -4, rows: ["......b.......", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."], extras: [] },
  critter: [
    { x: 0, top: -4, rows: ["......b.......", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."], extras: [] },
    { x: 0, top: -4, rows: [".......b......", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."], extras: [] },
    { x: 0, top: -4, rows: ["......b.......", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."], extras: [] },
    { x: 0, top: -4, rows: [".....b........", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."], extras: [] },
  ],
  onigiriStill: { x: 0, top: -4, rows: ["......bb......", "....aaaaaa....", "..aaaaaaaaaa..", "....bbbbbb...."], extras: [] },
  onigiri: [
    { x: 0, top: -4, rows: ["......b.......", "....aaaaaa....", "..aaaaaaaaaa..", "....bbbbbb...."], extras: [] },
    { x: 0, top: -4, rows: [".......b......", "....aaaaaa....", "..aaaaaaaaaa..", "....bbbbbb...."], extras: [] },
    { x: 0, top: -4, rows: ["......b.......", "....aaaaaa....", "..aaaaaaaaaa..", "....bbbbbb...."], extras: [] },
    { x: 0, top: -4, rows: [".....b........", "....aaaaaa....", "..aaaaaaaaaa..", "....bbbbbb...."], extras: [] },
  ],
  },
  chef: {
  ms: 450,
  critterStill: { x: 0, top: -4, rows: ["...cc.cc.cc...", "..cccccccccc..", "...cccccccc...", "...aaaaaaaa..."], extras: [] },
  critter: [
    { x: 0, top: -4, rows: ["...cc.cc.cc...", "..cccccccccc..", "...cccccccc...", "...aaaaaaaa..."], extras: [] },
    { x: 0, top: -5, rows: ["....cccccc....", "..cccccccccc..", "..cccccccccc..", "...cccccccc...", "...aaaaaaaa..."], extras: [] },
  ],
  onigiriStill: { x: 0, top: -4, rows: ["....cccccc....", "....cccccc....", ".....cccc.....", "....aaaaaa...."], extras: [] },
  onigiri: [
    { x: 0, top: -4, rows: ["....cccccc....", "....cccccc....", ".....cccc.....", "....aaaaaa...."], extras: [] },
    { x: 0, top: -5, rows: [".....cccc.....", "....cccccc....", "....cccccc....", ".....cccc.....", "....aaaaaa...."], extras: [] },
  ],
  },
  crown: {
  ms: 280,
  critterStill: { x: 0, top: -4, rows: ["...a..aa..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."], extras: [] },
  critter: [
    { x: 0, top: -4, rows: ["...c..aa..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."], extras: [] },
    { x: 0, top: -4, rows: ["...a..ca..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."], extras: [] },
    { x: 0, top: -4, rows: ["...a..ac..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."], extras: [] },
    { x: 0, top: -4, rows: ["...a..aa..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."], extras: [] },
  ],
  onigiriStill: { x: 0, top: -3, rows: ["....a.aa.a....", "....aaaaaa....", "....acaaca...."], extras: [] },
  onigiri: [
    { x: 0, top: -3, rows: ["....c.aa.a....", "....aaaaaa....", "....acaaca...."], extras: [] },
    { x: 0, top: -3, rows: ["....a.ca.a....", "....aaaaaa....", "....acaaca...."], extras: [] },
    { x: 0, top: -3, rows: ["....a.ac.a....", "....aaaaaa....", "....acaaca...."], extras: [] },
    { x: 0, top: -3, rows: ["....a.aa.a....", "....aaaaaa....", "....acaaca...."], extras: [] },
  ],
  },
  headphones: {
  ms: 320,
  critterStill: { x: 0, top: -3, rows: ["....aaaaaa....", "...a......a...", "..a........a.."], extras: [] },
  critter: [
    { x: 0, top: -3, rows: ["....aaaaaa....", "...a......a...", "..a........a.."], extras: [[-2, -1, "c"], [-1, -2, "c"]] },
    { x: 0, top: -3, rows: ["....aaaaaa....", "...a......a...", "..a........a.."], extras: [[-2, -2, "c"], [-1, -3, "c"]] },
    { x: 0, top: -3, rows: ["....aaaaaa....", "...a......a...", "..a........a.."], extras: [[-3, -3, "c"], [-2, -4, "c"], [-1, -1, "c"]] },
    { x: 0, top: -3, rows: ["....aaaaaa....", "...a......a...", "..a........a.."], extras: [[-2, -2, "c"]] },
  ],
  onigiriStill: { x: 0, top: -3, rows: [".....aaaa.....", "....a....a....", "...a......a..."], extras: [] },
  onigiri: [
    { x: 0, top: -3, rows: [".....aaaa.....", "....a....a....", "...a......a..."], extras: [[-1, -1, "c"], [0, -2, "c"]] },
    { x: 0, top: -3, rows: [".....aaaa.....", "....a....a....", "...a......a..."], extras: [[-1, -2, "c"], [0, -3, "c"]] },
    { x: 0, top: -3, rows: [".....aaaa.....", "....a....a....", "...a......a..."], extras: [[-2, -3, "c"], [-1, -4, "c"], [0, -1, "c"]] },
    { x: 0, top: -3, rows: [".....aaaa.....", "....a....a....", "...a......a..."], extras: [[-1, -2, "c"]] },
  ],
  },
  propeller: {
  ms: 120,
  critterStill: { x: 0, top: -4, rows: ["..cccc.aaaa...", ".......d......", "....ababab....", "...aaaaaaaa..."], extras: [] },
  critter: [
    { x: 0, top: -4, rows: ["..cccc.aaaa...", ".......d......", "....ababab....", "...aaaaaaaa..."], extras: [] },
    { x: 0, top: -4, rows: ["....cc.aa.....", ".......d......", "....ababab....", "...aaaaaaaa..."], extras: [] },
    { x: 0, top: -4, rows: ["..aaaa.cccc...", ".......d......", "....ababab....", "...aaaaaaaa..."], extras: [] },
    { x: 0, top: -4, rows: ["....aa.cc.....", ".......d......", "....ababab....", "...aaaaaaaa..."], extras: [] },
  ],
  onigiriStill: { x: 0, top: -4, rows: ["...ccc..aaa...", "......dd......", ".....abab.....", "....aaaaaa...."], extras: [] },
  onigiri: [
    { x: 0, top: -4, rows: ["...ccc..aaa...", "......dd......", ".....abab.....", "....aaaaaa...."], extras: [] },
    { x: 0, top: -4, rows: [".....cc.aa....", "......dd......", ".....abab.....", "....aaaaaa...."], extras: [] },
    { x: 0, top: -4, rows: ["...aaa..ccc...", "......dd......", ".....abab.....", "....aaaaaa...."], extras: [] },
    { x: 0, top: -4, rows: [".....aa.cc....", "......dd......", ".....abab.....", "....aaaaaa...."], extras: [] },
  ],
  },
  "top hat": {
  ms: 450,
  critterStill: { x: 0, top: -5, rows: ["...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."], extras: [] },
  critter: [
    { x: 0, top: -5, rows: ["...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."], extras: [] },
    { x: 0, top: -5, rows: ["...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."], extras: [] },
    { x: 0, top: -6, rows: ["...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."], extras: [] },
    { x: 0, top: -5, rows: ["...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."], extras: [] },
  ],
  onigiriStill: { x: 0, top: -4, rows: [".....aaaa.....", ".....aaaa.....", ".....bbbb.....", "...aaaaaaaa..."], extras: [] },
  onigiri: [
    { x: 0, top: -4, rows: [".....aaaa.....", ".....aaaa.....", ".....bbbb.....", "...aaaaaaaa..."], extras: [] },
    { x: 0, top: -4, rows: [".....aaaa.....", ".....aaaa.....", ".....bbbb.....", "...aaaaaaaa..."], extras: [] },
    { x: 0, top: -5, rows: [".....aaaa.....", ".....aaaa.....", ".....bbbb.....", "...aaaaaaaa..."], extras: [] },
    { x: 0, top: -4, rows: [".....aaaa.....", ".....aaaa.....", ".....bbbb.....", "...aaaaaaaa..."], extras: [] },
  ],
  },
  cowboy: {
  ms: 350,
  critterStill: { x: 0, top: -4, rows: [".....aaaa.....", "....aabbaa....", "b...aaaaaa...b", "bbbbbbbbbbbbbb"], extras: [] },
  critter: [
    { x: 0, top: -4, rows: [".....aaaa.....", "....aabbaa....", "b...aaaaaa...b", "bbbbbbbbbbbbbb"], extras: [] },
    { x: 0, top: -4, rows: [".....aaaa.....", "b...aabbaa....", "b...aaaaaa....", ".bbbbbbbbbbbbb"], extras: [] },
    { x: 0, top: -4, rows: [".....aaaa.....", "....aabbaa....", "b...aaaaaa...b", "bbbbbbbbbbbbbb"], extras: [] },
    { x: 0, top: -4, rows: [".....aaaa.....", "....aabbaa...b", "....aaaaaa...b", "bbbbbbbbbbbbb."], extras: [] },
  ],
  onigiriStill: { x: 0, top: -4, rows: ["......aa......", ".....abba.....", ".b..aaaaaa..b.", ".bbbbbbbbbbbb."], extras: [] },
  onigiri: [
    { x: 0, top: -4, rows: ["......aa......", ".....abba.....", ".b..aaaaaa..b.", ".bbbbbbbbbbbb."], extras: [] },
    { x: 0, top: -4, rows: ["......aa......", ".b...abba.....", ".b..aaaaaa....", "..bbbbbbbbbbb."], extras: [] },
    { x: 0, top: -4, rows: ["......aa......", ".....abba.....", ".b..aaaaaa..b.", ".bbbbbbbbbbbb."], extras: [] },
    { x: 0, top: -4, rows: ["......aa......", ".....abba...b.", "....aaaaaa..b.", ".bbbbbbbbbbb.."], extras: [] },
  ],
  },
  bandana: {
  ms: 200,
  critterStill: { x: 0, top: -1, rows: ["..aaaaaaaaaa..", "..acacacacac.."], extras: [[1, -1, "b"], [0, -2, "b"], [0, 0, "b"], [-1, 0, "b"]] },
  critter: [
    { x: 0, top: -1, rows: ["..aaaaaaaaaa..", "..acacacacac.."], extras: [[1, -1, "b"], [0, -2, "b"], [0, 0, "b"], [-1, 0, "b"]] },
    { x: 0, top: -1, rows: ["..aaaaaaaaaa..", "..acacacacac.."], extras: [[1, -1, "b"], [0, -1, "b"], [-1, -2, "b"], [0, 0, "b"], [-1, 1, "b"]] },
    { x: 0, top: -1, rows: ["..aaaaaaaaaa..", "..acacacacac.."], extras: [[1, -1, "b"], [0, -2, "b"], [-1, -2, "b"], [0, 0, "b"], [-1, 0, "b"]] },
  ],
  onigiriStill: { x: 0, top: 0, rows: ["...aaaaaaaa...", "..acacacacac.."], extras: [[2, 0, "b"], [1, -1, "b"], [1, 1, "b"], [0, 1, "b"]] },
  onigiri: [
    { x: 0, top: 0, rows: ["...aaaaaaaa...", "..acacacacac.."], extras: [[2, 0, "b"], [1, -1, "b"], [1, 1, "b"], [0, 1, "b"]] },
    { x: 0, top: 0, rows: ["...aaaaaaaa...", "..acacacacac.."], extras: [[2, 0, "b"], [1, 0, "b"], [0, -1, "b"], [1, 1, "b"], [0, 2, "b"]] },
    { x: 0, top: 0, rows: ["...aaaaaaaa...", "..acacacacac.."], extras: [[2, 0, "b"], [1, -1, "b"], [0, -1, "b"], [1, 1, "b"], [0, 1, "b"]] },
  ],
  },
};
export const HAT_COLORS = {
  teal: ['#4FB3A6', '#2F7F75'], blue: ['#6F8FD8', '#4A63A3'], yellow: ['#E8B94A', '#B08429'], green: ['#6DBE6A', '#468A44'],
  purple: ['#A68BD8', '#7660A6'], pink: ['#E58BB0', '#B05E80'], sky: ['#5EC4D6', '#3A8E9C'], ink: ['#E8E4DC', '#A8A39A'],
};
export const HAT_SHAPES = Object.keys(HAT_ART);
export const HAT_COLOR_NAMES = Object.keys(HAT_COLORS);

// One hat frame as pixel rects.
function hatFrame(f, fill) {
  let out = '';
  f.rows.forEach((row, r) => [...row].forEach((ch, c) => {
    if (fill[ch]) out += `<rect x="${f.x + c}" y="${f.top + r}" width="1" height="1" fill="${fill[ch]}"/>`;
  }));
  for (const [x, y, ch] of f.extras) out += `<rect x="${x}" y="${y}" width="1" height="1" fill="${fill[ch]}"/>`;
  return out;
}

// The hat: still, or (while working) its frames stacked, each shown in its
// time slot by a CSS frame class (app.css .hf-<frames>-<ms>-<slot>).
function hatSVG(hat, oni, faded, animate = false) {
  const art = HAT_ART[hat?.shape];
  if (!art) return '';
  const [main, shade] = HAT_COLORS[hat.color] || HAT_COLORS.ink;
  const fill = { a: main, b: shade, c: '#F7F4EC', d: '#0A0A0A' };
  const frames = oni ? art.onigiri : art.critter;
  let out = animate
    ? frames.map((f, i) => `<g class="hf-${frames.length}-${art.ms}-${i}${i ? ' hfo' : ''}">${hatFrame(f, fill)}</g>`).join('')
    : hatFrame(oni ? art.onigiriStill : art.critterStill, fill);
  if (hat.shape === 'headphones') {
    out += oni ? `<rect x="2" y="-0.5" width="1" height="2" fill="${shade}"/><rect x="11" y="-0.5" width="1" height="2" fill="${shade}"/>`
      : `<rect x="0" y="0" width="1" height="2" fill="${shade}"/><rect x="13" y="0" width="1" height="2" fill="${shade}"/>`;
  }
  return `<g class="hat"${faded ? ' opacity="0.45"' : ''}>${out}</g>`;
}

// Poses of a working agent: its hat animates; every other pose keeps it still.
const WORKING_POSES = new Set(['run', 'think', 'read', 'write', 'bash', 'web', 'plan', 'delegate', 'compact']);

export function spriteSVG(pose, species = 'claude', hat = null, opts = {}) {
  const bot = species === 'codex';
  const working = WORKING_POSES.has(pose);
  if (pose === 'write') pose = 'bash'; // writing looks like bash for now
  legs = bot ? botFeet : critterLegs;
  let body = bot ? `<path class="b" d="${BOT_BODY}"/>${pose === 'exit' ? '' : BOT_NORI}` : `<path class="b" d="${BODY}"/>`, eyes, feet = legs(LEGS_A), bubble = '', grp = '';
  // Arms: 1×2 beside the body, at their top row (2 = hanging down).
  const arm = (l, r, cls = '') => bot
    ? `<rect class="b ${cls}" x="1" y="${l - 1}" width="1" height="2"/><rect class="b ${cls}" x="12" y="${r - 1}" width="1" height="2"/>`
    : `<rect class="b ${cls}" x="0" y="${l}" width="1" height="2"/><rect class="b ${cls}" x="13" y="${r}" width="1" height="2"/>`;
  let arms = arm(2, 2), held = '';
  switch (pose) {
    case 'think':
      // The bridge's change (agents-deck keeps it still): pondering, not asleep.
      // A slow bob on the glance's 1.2 s, the raised hand tapping the chin
      // (rows 1 and 0), and the thought dots popping before they rise again.
      // Mirrors critter.cpp's Pose::Think.
      eyes = `<g class="glance">${eyesOpenAt(1, 1.5)}</g>`; grp = 'bob ponder';
      arms = bot ? arm(2, 1) : '<rect class="b" x="0" y="2" width="1" height="2"/>' +
        '<rect class="b la tap" x="13" y="1" width="1" height="2"/><rect class="b lb tap" x="13" y="0" width="1" height="2"/>';
      bubble = '<rect class="b p1" x="14.3" y="-0.9" width="0.6" height="0.6"/><rect class="b p2" x="15.3" y="-2.1" width="0.9" height="0.9"/><rect class="b p3" x="16.5" y="-3.8" width="1.3" height="1.3"/>';
      break;
    case 'read':
      eyes = `<g class="scan">${eyesOpen(2)}</g>`;
      bubble = pixels(['.##..', '#..#.', '#..#.', '.##..', '....#'], 14.6, 0.75, '', 0.75);
      break;
    case 'bash':
      eyes = eyesOpen(2); feet = legs(LEGS_A, 'la fast') + legs(LEGS_B, 'lb fast'); grp = 'bob fast';
      bubble = pixels(['#..', '.#.', '#..'], 14.5, 0.7, '', 0.8, -3.7) + '<rect class="b cursor" x="16.5" y="-1.7" width="1.2" height="0.5"/>';
      break;
    case 'web':
      eyes = '<rect class="c" x="4.4" y="1" width="1" height="2"/><rect class="c" x="9.4" y="1" width="1" height="2"/>';
      bubble = pixels(['.###.', '#.#.#', '#####', '#.#.#', '.###.'], 14.4, 0.72, 'la slow', 0.72, -4.2) +
        pixels(['.###.', '##.##', '#####', '##.##', '.###.'], 14.4, 0.72, 'lb slow', 0.72, -4.2);
      break;
    case 'plan':
      eyes = eyesOpen(2);
      bubble = [0, 1, 2].map((i) => { const y = -3.9 + i * 1.25;
        return `<rect class="b" x="14.4" y="${y}" width="0.9" height="0.9"/><rect class="c tick${i}" x="14.6" y="${y + 0.2}" width="0.5" height="0.5"/><rect class="b" x="15.6" y="${y + 0.25}" width="2.2" height="0.45"/>`; }).join('');
      break;
    case 'delegate':
      eyes = eyesOpen(2); feet = legs(LEGS_A, 'la') + legs(LEGS_B, 'lb'); grp = 'bob'; arms = arm(2, 1, 'la') + arm(1, 2, 'lb');
      bubble = MINI(14.4, 3.2, 0.26);
      break;
    case 'hungry':
      // Eyes up at a bobbing cookie; the mouth chomps open and shut.
      eyes = '<rect class="c" x="4.6" y="1" width="1" height="2"/><rect class="c" x="9.6" y="1" width="1" height="2"/>' +
        (bot ? '<rect class="c la slow" x="6.2" y="3.4" width="1.6" height="1.4"/><rect class="c lb slow" x="6.2" y="4" width="1.6" height="0.5"/>'
          : '<rect class="c la slow" x="6.2" y="4.2" width="2.2" height="1.6"/><rect class="c lb slow" x="6.2" y="4.8" width="2.2" height="0.5"/>');
      bubble = `<g class="bobc">${pixels(['.###.', '##.##', '#####', '#.###', '.###.'], 14.3, 0.75, '', 0.75, -4.2)}</g>`;
      break;
    case 'celebrate': {
      eyes = [4.5, 9.5].map((x) => `<path class="cx" stroke-width="0.55" d="M${x - 1} 3.3 L${x} 2.2 L${x + 1} 3.3"/>`).join('');
      grp = 'hop'; feet = `<g class="hop">${legs(LEGS_A)}</g>`; arms = arm(0, 0);
      // The finish flag in the raised hand: a pole and a 3×2 checker that waves
      // by swapping its squares.
      const checker = (phase, cls) => `<g class="${cls}">` + [0, 1, 2].flatMap((cx) => [0, 1].map((cy) =>
        `<rect class="${(cx + cy + phase) % 2 ? 'c' : 'fl'}" x="${15 + cx}" y="${-4 + cy}" width="1" height="1"/>`)).join('') + '</g>';
      held = `<rect class="fl" x="14" y="-4" width="1" height="6"/>${checker(0, 'la')}${checker(1, 'lb')}`;
      const plus = (x, y, i) => `<g class="spark s${i}"><rect class="b" x="${x - 0.5}" y="${y}" width="1.5" height="0.5"/><rect class="b" x="${x}" y="${y - 0.5}" width="0.5" height="1.5"/></g>`;
      bubble = plus(16.4, -6.2, 0) + plus(18.2, -4.4, 1);
      break;
    }
    case 'run':
      eyes = eyesOpen(2); feet = legs(LEGS_A, 'la') + legs(LEGS_B, 'lb'); bubble = DOTS; grp = 'bob'; arms = arm(2, 1, 'la') + arm(1, 2, 'lb'); break;
    case 'wait':
      eyes = eyesOpen(1); bubble = pixels(['###', '..#', '.#.', '...', '.#.'], 14.5, 1.1, 'blink'); break;
    case 'err':
      eyes = [3.8, 8.8].map((x) => `<path class="cx" stroke-width="0.55" d="M${x - 0.4} 1.8 l1.8 2.2 M${x + 1.4} 1.8 l-1.8 2.2"/>`).join('');
      arms = arm(0, 0); // both up in alarm
      bubble = pixels(['#', '#', '#', '.', '#'], 15.6, 1.2, 'bang'); break;
    case 'start':
      eyes = eyesOpen(2); bubble = DOTS; break;
    case 'compact': {
      // Compacting the conversation: squeezed flat and springing back, eyes
      // strained "> <", while a stack of pages presses into one.
      eyes = '<path class="cx" stroke-width="0.55" d="M3.6 1.9 L4.8 2.7 L3.6 3.5 M10.4 1.9 L9.2 2.7 L10.4 3.5"/>';
      grp = 'squash';
      const bar = (y, cls) => `<rect class="b ${cls}" x="14.6" y="${y}" width="3.2" height="0.6"/>`;
      bubble = bar(-3.8, 'press2') + bar(-2.7, 'press1') + bar(-1.6, '');
      break;
    }
    case 'exit':
      body += bot ? '<path class="c" d="M6 -1h2v1h1v1h1v2h1v2h1v1h-10v-1h1v-2h1v-2h1v-1h1z"/>' : '<path class="c" d="M2 1h10v4h-10z"/>';
      arms = ''; if (!bot) feet = '';
      eyes = '<rect class="b" x="4" y="3" width="1" height="1"/><rect class="b" x="9" y="3" width="1" height="1"/>'; break;
    default: // idle: asleep
      eyes = eyesShut;
      bubble = '<text class="zt zz" x="14.8" y="-0.6" font-size="3.2">z</text><text class="zt zz" x="16.6" y="-2.4" font-size="2.3">z</text>';
  }
  // The block's face sits half a row higher than the onigiri's.
  const face = bot ? eyes : `<g transform="translate(0 -0.5)">${eyes}</g>`;
  const hatG = hat ? hatSVG(hat, bot, pose === 'exit', working) : '';
  if (opts.bubble === false) bubble = '';
  return `<svg viewBox="0 -4 18 13" shape-rendering="crispEdges" aria-hidden="true" focusable="false"><g class="${grp}">${body}${arms}${face}${hatG}${held}</g>${feet}${bubble}</svg>`;
}
