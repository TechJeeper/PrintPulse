// Settings and printer connections, kept in the phone app's localStorage.
// Secrets (API keys, passwords, tokens) never go back into the settings page;
// the page only learns whether one is saved.

var CONFIG_KEY = 'printpulse.config';
var STATUS_KEY = 'printpulse.status';
var SECRET_FIELDS = ['apiKey', 'password', 'token', 'code'];
var TYPES = ['klipper', 'prusa', 'bambu'];
var MAX_SOURCES = 8;

var DEFAULT_SETTINGS = { interval: 30, theme: 0, vibe: true, wake: true, demo: false };

function read(key) {
  try {
    var raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : null;
  } catch (e) {
    return null;
  }
}

function write(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch (e) {}
}

function withDefaults(settings) {
  var out = {};
  for (var k in DEFAULT_SETTINGS) {
    out[k] = settings && settings[k] !== undefined ? settings[k] : DEFAULT_SETTINGS[k];
  }
  out.interval = Math.max(15, Math.min(300, parseInt(out.interval, 10) || 30));
  out.theme = out.theme === 1 ? 1 : 0;
  out.vibe = !!out.vibe;
  out.wake = !!out.wake;
  out.demo = !!out.demo;
  return out;
}

function load() {
  var saved = read(CONFIG_KEY);
  return {
    isNew: !saved,
    settings: withDefaults(saved && saved.settings),
    sources: (saved && saved.sources) || []
  };
}

function save(cfg) {
  write(CONFIG_KEY, { settings: cfg.settings, sources: cfg.sources });
}

function loadStatus() {
  return read(STATUS_KEY) || {};
}

function saveStatus(status) {
  write(STATUS_KEY, status);
}

function text(value, max) {
  return value === undefined || value === null ? '' : String(value).trim().slice(0, max || 200);
}

// Merge what the settings page sent back into the stored config.
function applyPage(cfg, page) {
  var previous = {};
  cfg.sources.forEach(function (s) {
    previous[s.id] = s;
  });

  var sources = [];
  (page.sources || []).forEach(function (ps) {
    if (!ps || TYPES.indexOf(ps.type) < 0 || sources.length >= MAX_SOURCES) return;
    var prev = previous[ps.id] || {};
    var secrets = {};
    var k;
    for (k in prev.secrets || {}) secrets[k] = prev.secrets[k];
    (ps.clear || []).forEach(function (field) {
      delete secrets[field];
    });
    var given = ps.secrets || {};
    SECRET_FIELDS.forEach(function (field) {
      if (given[field]) secrets[field] = text(given[field], 4000);
    });

    var src = {
      id: text(ps.id, 40) || 's' + Date.now().toString(36) + sources.length,
      type: ps.type,
      mode: ps.type === 'bambu' && ps.mode === 'bridge' ? 'bridge' : (ps.type === 'bambu' ? 'cloud' : ''),
      name: text(ps.name, 40),
      host: text(ps.host),
      user: text(ps.user, 60),
      email: text(ps.email, 120),
      region: ps.region === 'china' ? 'china' : 'global',
      hidden: (ps.hidden || []).map(String),
      needsCode: !!prev.needsCode,
      secrets: secrets
    };

    var accountChanged = prev.id && (prev.email !== src.email || prev.region !== src.region || prev.mode !== src.mode);
    if (src.type === 'bambu' && src.mode === 'cloud' && !given.token &&
        (given.password || given.code || accountChanged)) {
      delete secrets.token;  // signing in again
    }
    if (accountChanged && !given.code) {
      src.needsCode = false;
    }
    sources.push(src);
  });

  return { isNew: false, settings: withDefaults(page.settings), sources: sources };
}

function pageState(cfg, status) {
  return {
    settings: cfg.settings,
    sources: cfg.sources.map(function (s) {
      var secrets = s.secrets || {};
      return {
        id: s.id,
        type: s.type,
        mode: s.mode,
        name: s.name,
        host: s.host,
        user: s.user,
        email: s.email,
        region: s.region,
        hidden: s.hidden || [],
        needsCode: !!s.needsCode,
        has: { apiKey: !!secrets.apiKey, password: !!secrets.password, token: !!secrets.token }
      };
    }),
    status: status
  };
}

module.exports = {
  load: load,
  save: save,
  loadStatus: loadStatus,
  saveStatus: saveStatus,
  applyPage: applyPage,
  pageState: pageState
};
