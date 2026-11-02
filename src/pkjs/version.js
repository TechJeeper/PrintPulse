// Checks GitHub for a newer PrintPulse release. Dismissed versions stay quiet
// until version.json moves past them. Keep APP_VERSION in sync with package.json.

var http = require('./http');

var APP_VERSION = '1.1.0';
var CHECK_URL = 'https://raw.githubusercontent.com/TechJeeper/PrintPulse/main/version.json';
var DISMISS_KEY = 'printpulse.dismissedVersion';
var TOAST_KEY = 'printpulse.updateToast';
var DEFAULT_URL = 'https://github.com/TechJeeper/PrintPulse/releases/latest';

function read(key) {
  try {
    return localStorage.getItem(key) || '';
  } catch (e) {
    return '';
  }
}

function write(key, value) {
  try {
    if (value) localStorage.setItem(key, value);
    else localStorage.removeItem(key);
  } catch (e) {}
}

function parts(v) {
  return String(v || '').replace(/^v/i, '').split(/[.+-]/).map(function (p) {
    var n = parseInt(p, 10);
    return isNaN(n) ? 0 : n;
  });
}

function cmp(a, b) {
  var aa = parts(a);
  var bb = parts(b);
  var n = Math.max(aa.length, bb.length);
  for (var i = 0; i < n; i++) {
    var d = (aa[i] || 0) - (bb[i] || 0);
    if (d) return d < 0 ? -1 : 1;
  }
  return 0;
}

function isNewer(remote, local) {
  return cmp(remote, local) > 0;
}

function dismissed() {
  return read(DISMISS_KEY);
}

function dismiss(version) {
  if (version) write(DISMISS_KEY, String(version));
}

function toastShown() {
  return read(TOAST_KEY);
}

function markToast(version) {
  if (version) write(TOAST_KEY, String(version));
}

// cb(null) when nothing to show; otherwise cb({ version, url, message })
function check(cb) {
  http.getJson({ url: CHECK_URL, timeout: 6000 }, function (err, data) {
    if (err || !data || !data.version) return cb(null);
    var remote = String(data.version).replace(/^v/i, '');
    if (!isNewer(remote, APP_VERSION)) return cb(null);
    if (dismissed() === remote) return cb(null);
    cb({
      version: remote,
      url: data.url || DEFAULT_URL,
      message: data.message || ('PrintPulse ' + remote + ' is available')
    });
  });
}

module.exports = {
  APP_VERSION: APP_VERSION,
  check: check,
  dismiss: dismiss,
  toastShown: toastShown,
  markToast: markToast,
  isNewer: isNewer,
  _cmp: cmp
};
