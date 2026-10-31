// Prusa printers via PrusaLink (MK4/MK4S, MK3.9, XL, MINI+, CORE One, MK3S with
// PrusaLink). Uses the v1 API with digest auth, falling back to the older
// OctoPrint-style endpoints and API keys.
// https://github.com/prusa3d/Prusa-Link-Web/blob/master/spec/openapi.yaml

var http = require('../http');
var m = require('../model');

var runtime = {};  // source id -> { jobId, file }

var STATES = {
  IDLE: m.STATE.IDLE,
  READY: m.STATE.IDLE,
  BUSY: m.STATE.PREPARING,
  PRINTING: m.STATE.PRINTING,
  PAUSED: m.STATE.PAUSED,
  FINISHED: m.STATE.COMPLETE,
  STOPPED: m.STATE.CANCELLED,
  ERROR: m.STATE.ERROR,
  ATTENTION: m.STATE.ATTENTION
};

function auth(src) {
  var s = src.secrets || {};
  var opts = {};
  if (s.password) {
    opts.digest = { user: src.user || 'maker', pass: s.password };
  }
  if (s.apiKey) {
    opts.headers = { 'X-Api-Key': s.apiKey };
  }
  return opts;
}

function get(src, path, cb) {
  var opts = auth(src);
  opts.url = m.baseUrl(src.host) + path;
  http.getJson(opts, cb);
}

function denied(err) {
  return err && (err.status === 401 || err.status === 403);
}

function fileFor(src, jobId, cb) {
  var state = runtime[src.id] || (runtime[src.id] = {});
  if (jobId === undefined || jobId === null) return cb('');
  if (state.jobId === jobId) return cb(state.file);
  get(src, '/api/v1/job', function (err, job) {
    var file = !err && job && job.file ? (job.file.display_name || job.file.name || '') : '';
    state.jobId = jobId;
    state.file = m.cleanFileName(file);
    cb(state.file);
  });
}

function parseV1(src, st, file) {
  var printer = st.printer || {};
  var job = st.job || {};
  var p = m.printer({
    name: src.name || 'Prusa',
    kind: m.KIND.prusa,
    state: STATES[printer.state] !== undefined ? STATES[printer.state] : m.STATE.UNKNOWN,
    nozzle: m.temp(printer.temp_nozzle),
    nozzleTarget: Math.round(m.num(printer.target_nozzle) || 0),
    bed: m.temp(printer.temp_bed),
    bedTarget: Math.round(m.num(printer.target_bed) || 0),
    file: file
  });
  var progress = m.num(job.progress);
  if (progress !== null) p.progress = m.permille(progress / 100);
  if (p.state === m.STATE.COMPLETE) p.progress = 1000;
  p.timeLeft = m.seconds(job.time_remaining);
  p.elapsed = m.seconds(job.time_printing);
  if (p.state === m.STATE.ATTENTION) p.message = 'Check the printer';
  if (p.state === m.STATE.ERROR) p.message = 'Printer error';
  if (p.state === m.STATE.PREPARING) p.message = 'Busy';
  return p;
}

// PrusaLink 0.7 on MK3: OctoPrint-compatible /api/printer + /api/job.
function legacy(src, cb) {
  get(src, '/api/printer', function (err, pr) {
    if (err) return cb(denied(err) ? m.setupError('Wrong password or key') : err);
    get(src, '/api/job', function (err2, job) {
      job = job || {};
      var flags = (pr.state && pr.state.flags) || {};
      var temps = pr.temperature || {};
      var tool = temps.tool0 || {};
      var bed = temps.bed || {};
      var state = m.STATE.IDLE;
      if (flags.error) state = m.STATE.ERROR;
      else if (flags.paused || flags.pausing) state = m.STATE.PAUSED;
      else if (flags.printing) state = m.STATE.PRINTING;
      else if (flags.cancelling) state = m.STATE.CANCELLED;

      var progress = job.progress || {};
      var completion = m.num(progress.completion);
      var p = m.printer({
        name: src.name || 'Prusa',
        kind: m.KIND.prusa,
        state: state,
        nozzle: m.temp(tool.actual),
        nozzleTarget: Math.round(m.num(tool.target) || 0),
        bed: m.temp(bed.actual),
        bedTarget: Math.round(m.num(bed.target) || 0),
        file: m.cleanFileName(job.job && job.job.file ? (job.job.file.display || job.job.file.name) : ''),
        timeLeft: m.seconds(progress.printTimeLeft),
        elapsed: m.seconds(progress.printTime)
      });
      if (completion !== null && state !== m.STATE.IDLE) {
        p.progress = m.permille(completion > 1 ? completion / 100 : completion);
      }
      if (state === m.STATE.ERROR) p.message = (pr.state && pr.state.text) || 'Printer error';
      p.uid = m.hash(src.id);
      cb(null, [p]);
    });
  });
}

function fetch(src, cb) {
  get(src, '/api/v1/status', function (err, st) {
    if (err && err.status === 404) return legacy(src, cb);
    if (denied(err)) {
      var s = src.secrets || {};
      return cb(m.setupError(s.password || s.apiKey ? 'Wrong password' : 'Password needed'));
    }
    if (err) return cb(err);
    if (!st || !st.printer) return cb(new Error('Not a PrusaLink printer'));
    var jobId = st.job ? st.job.id : null;
    fileFor(src, jobId, function (file) {
      var p = parseV1(src, st, file);
      p.uid = m.hash(src.id);
      cb(null, [p]);
    });
  });
}

module.exports = {
  fetch: fetch,
  minInterval: 0,
  _parseV1: parseV1
};
