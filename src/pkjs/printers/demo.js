// Fake printers that move over time, for trying the app without hardware.

var m = require('../model');

function cycle(now, length, offset) {
  return ((now + offset * length) % length) / length;
}

function wobble(now, base, spread) {
  return base + Math.round(Math.sin(now / 37) * spread);
}

function fetch(src, cb) {
  var now = Math.floor(Date.now() / 1000);
  var printers = [];

  var voronLength = 2 * 3600 + 1800;
  var voron = cycle(now, voronLength, 0.45);
  printers.push(m.printer({
    name: 'Voron 2.4', kind: m.KIND.klipper, state: m.STATE.PRINTING,
    file: 'stealthburner_main_body', progress: m.permille(voron),
    timeLeft: Math.round(voronLength * (1 - voron)), elapsed: Math.round(voronLength * voron),
    nozzle: wobble(now, 245, 1), nozzleTarget: 245, bed: 110, bedTarget: 110,
    layer: Math.round(voron * 312), layerTotal: 312
  }));

  var x1Length = 95 * 60;
  var x1 = cycle(now, x1Length, 0.81);
  printers.push(m.printer({
    name: 'X1 Carbon', kind: m.KIND.bambu, state: m.STATE.PRINTING,
    file: 'Benchy_PLA_0.20mm', progress: m.permille(x1),
    timeLeft: Math.round(x1Length * (1 - x1)), elapsed: Math.round(x1Length * x1),
    nozzle: wobble(now, 220, 1), nozzleTarget: 220, bed: 55, bedTarget: 55,
    layer: Math.round(x1 * 240), layerTotal: 240
  }));

  printers.push(m.printer({
    name: 'Ender-3 S1', kind: m.KIND.klipper, state: m.STATE.PAUSED,
    file: 'gridfinity_base_4x3', progress: 372, timeLeft: 2 * 3600 + 14 * 60, elapsed: 80 * 60,
    message: 'Filament runout', nozzle: 210, nozzleTarget: 210, bed: 60, bedTarget: 60,
    layer: 41, layerTotal: 110
  }));

  printers.push(m.printer({
    name: 'MK4S', kind: m.KIND.prusa, state: m.STATE.IDLE,
    nozzle: 24, nozzleTarget: 0, bed: 23, bedTarget: 0
  }));

  printers.push(m.printer({
    name: 'P1S', kind: m.KIND.bambu, state: m.STATE.COMPLETE,
    file: 'cable_clips_x12', progress: 1000, elapsed: 3 * 3600 + 12 * 60,
    nozzle: 41, nozzleTarget: 0, bed: 38, bedTarget: 0, layer: 180, layerTotal: 180
  }));

  printers.forEach(function (p, i) {
    p.uid = m.hash(src.id + ':demo' + i);
  });
  cb(null, printers);
}

module.exports = { fetch: fetch, minInterval: 0 };
