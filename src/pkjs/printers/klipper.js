// Klipper via the Moonraker API (also what Mainsail and Fluidd talk to).
// https://moonraker.readthedocs.io/en/latest/web_api/

var http = require('../http');
var m = require('../model');

var QUERY = '/printer/objects/query?webhooks&print_stats&virtual_sdcard&display_status&extruder&heater_bed';

var runtime = {};  // source id -> { base, metaFile, meta }

function headers(src) {
  return src.secrets && src.secrets.apiKey ? { 'X-Api-Key': src.secrets.apiKey } : {};
}

// Mainsail/Fluidd proxy Moonraker on port 80; plain Moonraker listens on 7125.
function candidates(src) {
  var base = m.baseUrl(src.host);
  return m.hasPort(src.host) ? [base] : [base, base + ':7125'];
}

function query(src, cb) {
  var state = runtime[src.id] || (runtime[src.id] = {});
  var bases = state.base ? [state.base] : candidates(src);
  var firstError = null;
  (function next(i) {
    if (i >= bases.length) {
      state.base = null;
      cb(firstError);
      return;
    }
    http.getJson({ url: bases[i] + QUERY, headers: headers(src) }, function (err, data) {
      if (!err && data && data.result && data.result.status) {
        state.base = bases[i];
        cb(null, data.result.status, state);
        return;
      }
      if (err && err.status === 401) {
        cb(m.setupError('API key needed'));
        return;
      }
      firstError = firstError || err || new Error('Not a Moonraker server');
      next(i + 1);
    });
  })(0);
}

function withMetadata(src, state, filename, cb) {
  if (!filename) return cb(null);
  if (state.metaFile === filename) return cb(state.meta);
  var url = state.base + '/server/files/metadata?filename=' + encodeURIComponent(filename);
  http.getJson({ url: url, headers: headers(src) }, function (err, data) {
    state.metaFile = filename;
    state.meta = !err && data && data.result ? data.result : null;
    cb(state.meta);
  });
}

function estimateTimeLeft(progress, printDuration, meta) {
  var slicer = meta && m.num(meta.estimated_time) ? meta.estimated_time - printDuration : null;
  var byFile = progress > 0.01 && printDuration > 0 ? printDuration / progress - printDuration : null;
  var left;
  if (slicer !== null && byFile !== null) {
    // Trust the slicer early on and the measured pace as the print goes on.
    left = slicer * (1 - progress) + byFile * progress;
  } else {
    left = slicer !== null ? slicer : byFile;
  }
  return left === null ? -1 : Math.max(0, Math.round(left));
}

var STATES = {
  standby: m.STATE.IDLE,
  printing: m.STATE.PRINTING,
  paused: m.STATE.PAUSED,
  complete: m.STATE.COMPLETE,
  cancelled: m.STATE.CANCELLED,
  error: m.STATE.ERROR
};

function parse(src, status, meta) {
  var wh = status.webhooks || {};
  var ps = status.print_stats || {};
  var vsd = status.virtual_sdcard || {};
  var ds = status.display_status || {};
  var ext = status.extruder || {};
  var bed = status.heater_bed || {};

  var p = m.printer({
    name: src.name || 'Klipper',
    kind: m.KIND.klipper,
    nozzle: m.temp(ext.temperature),
    nozzleTarget: Math.round(m.num(ext.target) || 0),
    bed: m.temp(bed.temperature),
    bedTarget: Math.round(m.num(bed.target) || 0)
  });

  if (wh.state && wh.state !== 'ready') {
    p.state = wh.state === 'startup' ? m.STATE.PREPARING : m.STATE.ERROR;
    p.message = m.firstLine(wh.state_message) || (wh.state === 'startup' ? 'Starting up' : 'Klipper ' + wh.state);
    return p;
  }

  p.state = STATES[ps.state] !== undefined ? STATES[ps.state] : m.STATE.UNKNOWN;
  p.file = m.cleanFileName(ps.filename);
  var progress = m.num(ds.progress);
  if (progress === null) progress = m.num(vsd.progress) || 0;
  var printDuration = m.num(ps.print_duration) || 0;

  if (p.state === m.STATE.PRINTING && printDuration === 0 && !(m.num(ps.filament_used) > 0)) {
    p.state = m.STATE.PREPARING;
    p.message = 'Heating up';
  }
  if (p.state === m.STATE.COMPLETE) progress = 1;
  if (p.state !== m.STATE.IDLE) {
    p.progress = m.permille(progress);
    p.elapsed = m.seconds(ps.total_duration);
  }
  if (p.state === m.STATE.PRINTING || p.state === m.STATE.PAUSED || p.state === m.STATE.PREPARING) {
    p.timeLeft = estimateTimeLeft(progress, printDuration, meta);
  }

  var info = ps.info || {};
  if (m.num(info.current_layer) !== null) p.layer = info.current_layer;
  if (m.num(info.total_layer) !== null) {
    p.layerTotal = info.total_layer;
  } else if (meta && m.num(meta.layer_count) !== null) {
    p.layerTotal = meta.layer_count;
  }
  if (p.state === m.STATE.IDLE) {
    p.layer = -1;
    p.layerTotal = -1;
  }

  if (p.state === m.STATE.ERROR) {
    p.message = m.firstLine(ps.message) || 'Print error';
  } else if (!p.message && ds.message && p.state !== m.STATE.IDLE) {
    p.message = m.firstLine(ds.message);
  }
  return p;
}

function fetch(src, cb) {
  query(src, function (err, status, state) {
    if (err) return cb(err);
    var ps = status.print_stats || {};
    var active = ps.state === 'printing' || ps.state === 'paused';
    var go = function (meta) {
      var p = parse(src, status, meta);
      p.uid = m.hash(src.id);
      cb(null, [p]);
    };
    if (active) {
      withMetadata(src, state, ps.filename, go);
    } else {
      go(null);
    }
  });
}

module.exports = {
  fetch: fetch,
  minInterval: 0,
  _parse: parse
};
