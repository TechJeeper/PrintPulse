// Shared constants and helpers. Enums must match src/c/model.h and src/c/comm.h.

var KIND = { klipper: 0, bambu: 1, prusa: 2 };

var STATE = {
  UNKNOWN: 0,
  OFFLINE: 1,
  IDLE: 2,
  PREPARING: 3,
  PRINTING: 4,
  PAUSED: 5,
  ATTENTION: 6,
  COMPLETE: 7,
  CANCELLED: 8,
  ERROR: 9
};

var STATE_LABEL = ['Waiting', 'Offline', 'Ready', 'Preparing', 'Printing', 'Paused',
  'Needs attention', 'Complete', 'Cancelled', 'Error'];

var FLAG = { ESTIMATED: 1, STALE: 2 };

var CMD = { SYNC_BEGIN: 1, PRINTER: 2, SYNC_DONE: 3, SETTINGS: 4, REFRESH: 5, NOTICE: 6, ACTION: 7 };
var ACTION = { PAUSE: 1, RESUME: 2, STOP: 3 };

var TEMP_NONE = -999;
var MAX_PRINTERS = 8;
var NAME_MAX = 23;
var TEXT_MAX = 47;

function printer(fields) {
  var p = {
    uid: 0,
    name: '',
    kind: KIND.klipper,
    state: STATE.UNKNOWN,
    flags: 0,
    progress: -1,       // 0..1000
    timeLeft: -1,       // seconds
    elapsed: -1,        // seconds
    file: '',
    message: '',
    nozzle: TEMP_NONE,
    nozzleTarget: 0,
    bed: TEMP_NONE,
    bedTarget: 0,
    layer: -1,
    layerTotal: -1,
    stamp: 0
  };
  for (var k in fields) {
    if (fields.hasOwnProperty(k)) p[k] = fields[k];
  }
  return p;
}

function num(v) {
  var n = typeof v === 'string' ? parseFloat(v) : v;
  return typeof n === 'number' && isFinite(n) ? n : null;
}

function temp(v) {
  var n = num(v);
  return n === null ? TEMP_NONE : Math.round(n);
}

function seconds(v) {
  var n = num(v);
  return n === null || n < 0 ? -1 : Math.round(n);
}

function permille(fraction) {
  var n = num(fraction);
  if (n === null) return -1;
  return Math.max(0, Math.min(1000, Math.round(n * 1000)));
}

// "gcodes/parts/Voron_Benchy_0.2mm_PLA.gcode" -> "Voron_Benchy_0.2mm_PLA"
function cleanFileName(path) {
  if (!path) return '';
  var name = String(path).split('/').pop();
  return name.replace(/\.(gcode\.3mf|bgcode|gcode|gco|g|3mf)$/i, '');
}

function firstLine(text) {
  return text ? String(text).split('\n')[0].trim() : '';
}

// Accepts "192.168.1.5", "voron.local:7125" or a full URL.
function baseUrl(host) {
  var h = String(host || '').trim().replace(/\/+$/, '');
  if (!/^[a-z]+:\/\//i.test(h)) h = 'http://' + h;
  return h;
}

function hasPort(host) {
  return /^([a-z]+:\/\/)?(\[[^\]]+\]|[^\/:]+):\d+/i.test(String(host || '').trim());
}

// Stable 31-bit id so the watch can follow a printer across list changes.
function hash(text) {
  var h = 5381;
  for (var i = 0; i < text.length; i++) {
    h = ((h << 5) + h + text.charCodeAt(i)) | 0;
  }
  return (h & 0x7fffffff) || 1;
}

function utf8Length(text) {
  return unescape(encodeURIComponent(text)).length;
}

// Shorten to fit the watch's fixed-size UTF-8 buffers without splitting a
// character.
function clip(text, maxBytes) {
  text = text ? String(text) : '';
  if (utf8Length(text) <= maxBytes) return text;
  while (text.length && utf8Length(text) + 3 > maxBytes) {
    text = text.slice(0, -1);
    var last = text.charCodeAt(text.length - 1);
    if (last >= 0xd800 && last <= 0xdbff) text = text.slice(0, -1);
  }
  return text + '…';
}

// An error the user fixes in settings (sign-in, password, API key).
function setupError(message) {
  var e = new Error(message);
  e.setup = true;
  return e;
}

function describe(p) {
  var label = STATE_LABEL[p.state] || 'Waiting';
  if (p.progress >= 0 && (p.state === STATE.PRINTING || p.state === STATE.PAUSED)) {
    return label + ' ' + Math.round(p.progress / 10) + '%';
  }
  return label;
}

module.exports = {
  KIND: KIND,
  STATE: STATE,
  FLAG: FLAG,
  CMD: CMD,
  ACTION: ACTION,
  TEMP_NONE: TEMP_NONE,
  MAX_PRINTERS: MAX_PRINTERS,
  NAME_MAX: NAME_MAX,
  TEXT_MAX: TEXT_MAX,
  printer: printer,
  num: num,
  temp: temp,
  seconds: seconds,
  permille: permille,
  cleanFileName: cleanFileName,
  firstLine: firstLine,
  baseUrl: baseUrl,
  hasPort: hasPort,
  hash: hash,
  clip: clip,
  setupError: setupError,
  describe: describe
};
