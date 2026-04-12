// Standalone Node.js replica of the 3D demo's CA, used to debug the
// oscillation the user reported. Mirrors docs/water_demo/demo3d.html
// tickCA() exactly, on the same terrain.
//
// Run: node scripts/water_ca_debug.mjs [maxTicks=200]

const GX = 16, GY = 14, GZ = 16;
const FALLING_FLAG = 0x80;
const LEVEL_MASK = 0x07;
const AIR = 0, STONE = 1, WATER = 2;

const cellType = new Uint8Array(GX * GY * GZ);
const cellWater = new Uint8Array(GX * GY * GZ);

const idx = (x, y, z) => (y * GZ + z) * GX + x;
const inBounds = (x, y, z) => x >= 0 && x < GX && y >= 0 && y < GY && z >= 0 && z < GZ;
const flowLevel = (r) => r & LEVEL_MASK;
const isFalling = (r) => (r & FALLING_FLAG) !== 0;
const isSource = (r) => (r & (LEVEL_MASK | FALLING_FLAG)) === 0;

function buildTerrain() {
  cellType.fill(AIR);
  cellWater.fill(0);
  for (let x = 0; x < GX; x++)
    for (let z = 0; z < GZ; z++) {
      cellType[idx(x, 0, z)] = STONE;
      cellType[idx(x, 1, z)] = STONE;
    }
  for (let x = 6; x < 10; x++)
    for (let z = 6; z < 10; z++)
      for (let y = 2; y <= 5; y++)
        cellType[idx(x, y, z)] = STONE;
  for (let x = 11; x < 13; x++)
    for (let z = 5; z < 11; z++)
      cellType[idx(x, 2, z)] = STONE;
  for (let x = 1; x < 4; x++)
    for (let z = 5; z < 11; z++) {
      cellType[idx(x, 0, z)] = AIR;
      cellType[idx(x, 1, z)] = AIR;
    }
  for (let z = 4; z < 12; z++) {
    cellType[idx(0, 0, z)] = STONE;
    cellType[idx(0, 1, z)] = STONE;
    cellType[idx(4, 0, z)] = STONE;
    cellType[idx(4, 1, z)] = STONE;
  }
  for (let x = 0; x < 5; x++) {
    cellType[idx(x, 0, 4)] = STONE;
    cellType[idx(x, 1, 4)] = STONE;
    cellType[idx(x, 0, 11)] = STONE;
    cellType[idx(x, 1, 11)] = STONE;
  }
}

function placeSource() {
  cellType[idx(8, 6, 8)] = WATER;
  cellWater[idx(8, 6, 8)] = 0;
}

// Record of per-cell changes in a tick: { x, y, z, from: {type, level, flag}, to: {...} }
let tickLog = [];

function logChange(x, y, z, fromType, fromWater, toType, toWater) {
  tickLog.push({ x, y, z, fromType, fromWater, toType, toWater });
}

function tickCA() {
  tickLog = [];
  const active = [];
  for (let y = 0; y < GY; y++)
    for (let z = 0; z < GZ; z++)
      for (let x = 0; x < GX; x++)
        if (cellType[idx(x, y, z)] === WATER) active.push([x, y, z]);
  active.sort((a, b) => b[1] - a[1]);

  const HDIRS = [[1, 0], [-1, 0], [0, 1], [0, -1]];
  let changed = 0;

  // Pre-pass: determine flowing decays (ripple source).
  const flowingDecaySet = new Set();
  for (const [x, y, z] of active) {
    const raw = cellWater[idx(x, y, z)];
    if (isSource(raw) || isFalling(raw)) continue;
    const lvl = flowLevel(raw);
    if (y + 1 < GY && cellType[idx(x, y + 1, z)] === WATER) {
      const araw = cellWater[idx(x, y + 1, z)];
      if (isSource(araw) || isFalling(araw)) continue;
      if (flowLevel(araw) < lvl) continue;
    }
    let supported = false;
    for (const [dx, dz] of HDIRS) {
      const nx = x + dx, nz = z + dz;
      if (!inBounds(nx, y, nz)) continue;
      if (cellType[idx(nx, y, nz)] !== WATER) continue;
      const nraw = cellWater[idx(nx, y, nz)];
      if (isFalling(nraw)) { supported = true; break; }
      if (flowLevel(nraw) < lvl) { supported = true; break; }
    }
    if (!supported) flowingDecaySet.add(idx(x, y, z));
  }

  for (const [x, y, z] of active) {
    if (flowingDecaySet.has(idx(x, y, z))) continue;
    if (cellType[idx(x, y, z)] !== WATER) continue;
    const raw = cellWater[idx(x, y, z)];
    const lvl = flowLevel(raw);
    const falling = isFalling(raw);
    const source = isSource(raw);

    // Rule 1: gravity
    if (y - 1 >= 0 && cellType[idx(x, y - 1, z)] === AIR) {
      const bi = idx(x, y - 1, z);
      logChange(x, y - 1, z, AIR, 0, WATER, FALLING_FLAG);
      cellType[bi] = WATER;
      cellWater[bi] = FALLING_FLAG;
      changed++;
      continue;
    }

    let landed = false;
    if (y === 0) {
      landed = true;
    } else {
      const below = cellType[idx(x, y - 1, z)];
      if (below === AIR) landed = false;
      else if (below === WATER) landed = !isFalling(cellWater[idx(x, y - 1, z)]);
      else landed = true;
    }

    const spreadLevel = falling ? 0 : lvl;
    if (spreadLevel < 7 && landed) {
      for (const [dx, dz] of HDIRS) {
        const nx = x + dx, nz = z + dz;
        if (!inBounds(nx, y, nz)) continue;
        const ni = idx(nx, y, nz);
        const nt = cellType[ni];
        if (nt === AIR) {
          logChange(nx, y, nz, AIR, 0, WATER, spreadLevel + 1);
          cellType[ni] = WATER;
          cellWater[ni] = spreadLevel + 1;
          changed++;
        } else if (nt === WATER) {
          const nraw = cellWater[ni];
          if (isSource(nraw) || isFalling(nraw)) continue;
          if (flowLevel(nraw) > spreadLevel + 1) {
            logChange(nx, y, nz, WATER, nraw, WATER, spreadLevel + 1);
            cellWater[ni] = spreadLevel + 1;
            changed++;
          }
        }
      }
    }

    if (falling) {
      const aboveIsWater = y + 1 < GY && cellType[idx(x, y + 1, z)] === WATER;
      if (!aboveIsWater) {
        logChange(x, y, z, WATER, raw, AIR, 0);
        cellType[idx(x, y, z)] = AIR;
        cellWater[idx(x, y, z)] = 0;
        changed++;
      }
    }
  }

  for (const cellIdx of flowingDecaySet) {
    logChange(-1, -1, -1, WATER, 0, AIR, 0); // minimal log
    cellType[cellIdx] = AIR;
    cellWater[cellIdx] = 0;
    changed++;
  }
  return changed;
}

function stateHash() {
  // Pack the live state into a string for oscillation detection
  let h = '';
  for (let y = 0; y < GY; y++)
    for (let z = 0; z < GZ; z++)
      for (let x = 0; x < GX; x++) {
        const i = idx(x, y, z);
        if (cellType[i] === WATER) {
          h += `(${x},${y},${z}):${cellWater[i].toString(16)};`;
        }
      }
  return h;
}

function fmtWater(raw) {
  if ((raw & FALLING_FLAG) !== 0) return `FALL`;
  if ((raw & LEVEL_MASK) === 0 && (raw & FALLING_FLAG) === 0) return `SRC`;
  return `L${raw & LEVEL_MASK}`;
}

function fmtChange(c) {
  const from = c.fromType === AIR ? 'AIR' : fmtWater(c.fromWater);
  const to = c.toType === AIR ? 'AIR' : fmtWater(c.toWater);
  return `(${c.x},${c.y},${c.z}) ${from}→${to}`;
}

// --- Run ---
const maxTicks = parseInt(process.argv[2] || '300', 10);
const mode = process.argv[3] || 'fill'; // 'fill' or 'drain'

buildTerrain();
placeSource();

const hashHistory = new Map(); // hash → first tick seen
let oscillationFound = null;

console.log(`starting ${maxTicks} ticks (mode=${mode}) with source at (8,6,8)...`);

let drainStartTick = -1;

for (let t = 0; t < maxTicks; t++) {
  const changed = tickCA();
  const hash = stateHash();

  if (hashHistory.has(hash)) {
    const firstSeen = hashHistory.get(hash);
    if (t - firstSeen > 1 && drainStartTick < 0) {
      oscillationFound = { tick: t, cycleStart: firstSeen, period: t - firstSeen };
      console.log(`\n*** OSCILLATION DETECTED at tick ${t} ***`);
      console.log(`    state matches tick ${firstSeen}, period = ${t - firstSeen}`);
      break;
    }
  } else {
    hashHistory.set(hash, t);
  }

  if (changed === 0) {
    console.log(`tick ${t}: CONVERGED (0 changes)`);
    if (mode === 'drain' && drainStartTick < 0) {
      console.log(`\n--- REMOVING SOURCE at (8,6,8) ---`);
      cellType[idx(8, 6, 8)] = AIR;
      cellWater[idx(8, 6, 8)] = 0;
      drainStartTick = t + 1;
      hashHistory.clear();
      continue;
    }
    break;
  }

  if (drainStartTick >= 0) {
    // During drain, log every tick's changes so we can watch the ripple
    console.log(`drain+${t - drainStartTick}: ${changed} changes`);
    for (const c of tickLog) console.log(`  ${fmtChange(c)}`);
  }
}

if (oscillationFound) {
  // Replay the oscillation cycle to identify the oscillating cells
  console.log(`\n--- Replaying oscillation cycle (period ${oscillationFound.period}) ---`);
  buildTerrain();
  placeSource();
  // Re-run until we're in the cycle
  for (let t = 0; t < oscillationFound.cycleStart; t++) tickCA();
  const inCycleCells = new Set();
  for (let t = 0; t < oscillationFound.period + 1; t++) {
    const changed = tickCA();
    console.log(`cycle+${t}: ${changed} changes`);
    for (const c of tickLog) {
      console.log(`  ${fmtChange(c)}`);
      inCycleCells.add(`${c.x},${c.y},${c.z}`);
    }
  }
  console.log(`\nCells involved in oscillation: ${[...inCycleCells].join(' | ')}`);
}
