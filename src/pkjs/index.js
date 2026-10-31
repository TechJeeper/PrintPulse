// PrintPulse phone side: polls each configured printer, normalises the data
// and streams it to the watch over AppMessage. Also serves the settings page.

var keys = require('message_keys');
var m = require('./model');
var store = require('./store');
var configPage = require('./config_page');

var DRIVERS = {
  klipper: require('./klipper'),
  prusa: require('./prusa'),
  bambu: require('./bambu'),
  demo: require('./demo')
};

var DEMO_SOURCE = { id: 'demo', type: 'demo', name: 'Demo' };
var SOURCE_TIMEOUT = 30000;
var STALE_LIMIT = 12 * 3600;  // show last known data this long when unreachable
var LASTGOOD_KEY = 'printpulse.lastgood';

var cfg = null;
var status = {};       // source id -> { ok, text, at, devices }
var lastGood = {};     // source id -> { at, printers }
var fresh = {};        // source id -> time of last successful fetch (ms)
var lastSent = [];     // watch slot -> signature of what it holds
var pollTimer = null;
var polling = false;
var pollAgain = null;
var queue = [];
var sending = false;

// ---- AppMessage queue ----------------------------------------------------

function send(dict, onFail) {
  queue.push({ dict: dict, tries: 0, onFail: onFail });
  pump();
}

function pump() {
  if (sending || !queue.length) return;
  sending = true;
  var item = queue[0];
  Pebble.sendAppMessage(item.dict, function () {
    queue.shift();
    sending = false;
    pump();
  }, function () {
    sending = false;
    item.tries += 1;
    if (item.tries >= 3) {
      queue.shift();
      if (item.onFail) item.onFail();
    }
    setTimeout(pump, 300 * item.tries);
  });
}

function toDict(p, index, count) {
  return {
    Cmd: m.CMD.PRINTER,
    Index: index,
    Count: count,
    Uid: p.uid,
    Name: m.clip(p.name, m.NAME_MAX),
    Kind: p.kind,
    State: p.state,
    Flags: p.flags,
    Progress: p.progress,
    TimeLeft: p.timeLeft,
    Elapsed: p.elapsed,
    Stamp: p.stamp,
    File: m.clip(p.file, m.TEXT_MAX),
    Message: m.clip(p.message, m.TEXT_MAX),
    Nozzle: p.nozzle,
    NozzleTarget: p.nozzleTarget,
    Bed: p.bed,
    BedTarget: p.bedTarget,
    Layer: p.layer,
    LayerTotal: p.layerTotal
  };
}

// The stamp alone changing doesn't need a resend: the watch only uses it to
// count down, and time left changes whenever that matters.
function signature(dict) {
  var copy = {};
  for (var k in dict) {
    if (k !== 'Stamp') copy[k] = dict[k];
  }
  return JSON.stringify(copy);
}

function sendSettings() {
  var s = cfg.settings;
  send({
    Cmd: m.CMD.SETTINGS,
    Theme: s.theme,
    VibeOnDone: s.vibe ? 1 : 0,
    WakeOnDone: s.wake ? 1 : 0,
    Interval: s.interval
  });
}

// ---- polling -------------------------------------------------------------

function findSource(id) {
  for (var i = 0; i < cfg.sources.length; i++) {
    if (cfg.sources[i].id === id) return cfg.sources[i];
  }
  return null;
}

// Lets a driver persist things it learns (e.g. a Bambu access token).
function contextFor(src) {
  return {
    update: function (patch) {
      var targets = [src];
      var current = findSource(src.id);
      if (current && current !== src) targets.push(current);
      targets.forEach(function (t) {
        for (var k in patch) {
          if (k !== 'secrets') t[k] = patch[k];
        }
        if (patch.secrets) {
          t.secrets = t.secrets || {};
          for (var s in patch.secrets) {
            if (patch.secrets[s] === null) {
              delete t.secrets[s];
            } else {
              t.secrets[s] = patch.secrets[s];
            }
          }
        }
      });
      store.save(cfg);
    },
    setDevices: function (devices) {
      status[src.id] = status[src.id] || {};
      status[src.id].devices = devices;
    }
  };
}

function defaultName(src) {
  if (src.type === 'bambu') return src.mode === 'bridge' ? 'Bambu bridge' : 'Bambu Lab';
  return src.type === 'prusa' ? 'Prusa' : 'Klipper';
}

function summary(printers) {
  if (!printers.length) return 'Connected, no printers';
  return printers.map(function (p) {
    return (printers.length > 1 ? p.name + ': ' : '') + m.describe(p);
  }).join(' · ');
}

function fetchSource(src, opts, cb) {
  var driver = DRIVERS[src.type];
  var now = Date.now();
  var min = typeof driver.minInterval === 'function' ? driver.minInterval(src) : driver.minInterval;
  if (opts.manual) min = Math.min(min, 20);
  var good = lastGood[src.id];
  if (!opts.fresh && good && fresh[src.id] && now - fresh[src.id] < min * 1000) {
    return cb(good.printers);
  }

  var done = false;
  var guard = setTimeout(function () {
    finish(new Error('Timed out'));
  }, SOURCE_TIMEOUT);

  function finish(err, printers) {
    if (done) return;
    done = true;
    clearTimeout(guard);
    var stamp = Math.floor(Date.now() / 1000);
    var prev = status[src.id] || {};

    if (!err) {
      printers.forEach(function (p) {
        if (!p.stamp) p.stamp = stamp;
        if (!p.uid) p.uid = m.hash(src.id);
      });
      fresh[src.id] = Date.now();
      lastGood[src.id] = { at: stamp, printers: printers };
      status[src.id] = { ok: true, text: summary(printers), at: Date.now(), devices: prev.devices };
      return cb(printers);
    }

    status[src.id] = { ok: false, text: err.message, at: Date.now(), devices: prev.devices };
    if (!err.setup && good && good.printers.length && stamp - good.at < STALE_LIMIT) {
      cb(good.printers.map(function (p) {
        var copy = m.printer(p);
        copy.flags |= m.FLAG.STALE;
        return copy;
      }));
      return;
    }
    cb([m.printer({
      uid: m.hash(src.id),
      name: src.name || defaultName(src),
      kind: m.KIND[src.type] || 0,
      state: err.setup ? m.STATE.ATTENTION : m.STATE.OFFLINE,
      message: err.message,
      stamp: stamp
    })]);
  }

  try {
    driver.fetch(src, finish, contextFor(src));
  } catch (e) {
    console.log('PrintPulse: ' + src.type + ' failed: ' + e);
    finish(new Error('Unexpected reply'));
  }
}

function activeSources() {
  var list = cfg.sources.slice();
  if (cfg.settings.demo) list.push(DEMO_SOURCE);
  return list;
}

function publish(printers) {
  printers = printers.slice(0, m.MAX_PRINTERS);
  printers.forEach(function (p, i) {
    var dict = toDict(p, i, printers.length);
    var sig = signature(dict);
    if (lastSent[i] === sig) return;
    lastSent[i] = sig;
    send(dict, function () {
      lastSent[i] = null;  // dropped, resend next time
    });
  });
  lastSent.length = printers.length;
  send({ Cmd: m.CMD.SYNC_DONE, Count: printers.length });
}

function poll(opts) {
  opts = opts || {};
  if (polling) {
    pollAgain = opts;  // e.g. settings saved mid-poll
    return;
  }
  polling = true;
  send({ Cmd: m.CMD.SYNC_BEGIN });

  var sources = activeSources();
  var results = [];
  var pending = sources.length;
  var complete = function () {
    polling = false;
    publish([].concat.apply([], results));
    store.saveStatus(status);
    try {
      localStorage.setItem(LASTGOOD_KEY, JSON.stringify(lastGood));
    } catch (e) {}
    if (pollAgain) {
      var next = pollAgain;
      pollAgain = null;
      poll(next);
    }
  };
  if (!pending) return complete();
  sources.forEach(function (src, i) {
    fetchSource(src, opts, function (printers) {
      results[i] = printers;
      if (--pending === 0) complete();
    });
  });
}

function schedule() {
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = setInterval(function () {
    poll();
  }, cfg.settings.interval * 1000);
}

// ---- settings page -------------------------------------------------------

function configUrl() {
  var json = JSON.stringify(store.pageState(cfg, status)).replace(/</g, '\\u003c');
  var returnTo = Pebble.platform === 'pypkjs' ? '$$$RETURN_TO$$$' : 'pebblejs://close#';
  var html = configPage
    .replace('"__PRINTPULSE_STATE__"', function () { return json; })
    .replace('__RETURN_TO__', function () { return returnTo; });
  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

function parseResponse(response) {
  try {
    return JSON.parse(response);
  } catch (e) {
    try {
      return JSON.parse(decodeURIComponent(response));
    } catch (e2) {
      return null;
    }
  }
}

// ---- events --------------------------------------------------------------

Pebble.addEventListener('ready', function () {
  cfg = store.load();
  if (cfg.isNew && Pebble.platform === 'pypkjs') {
    cfg.settings.demo = true;  // the emulator shows demo printers until set up
  }
  status = store.loadStatus();
  try {
    lastGood = JSON.parse(localStorage.getItem(LASTGOOD_KEY)) || {};
  } catch (e) {
    lastGood = {};
  }
  sendSettings();
  poll();
  schedule();
});

Pebble.addEventListener('appmessage', function (e) {
  var payload = e.payload || {};
  var cmd = payload.Cmd !== undefined ? payload.Cmd : payload[keys.Cmd];
  if (cmd === m.CMD.REFRESH) poll({ manual: true });
});

Pebble.addEventListener('showConfiguration', function () {
  if (!cfg) cfg = store.load();
  Pebble.openURL(configUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  var page = e && e.response ? parseResponse(e.response) : null;
  if (!page || !page.sources) return;
  cfg = store.applyPage(cfg, page);
  store.save(cfg);
  var kept = {};
  cfg.sources.forEach(function (s) {
    kept[s.id] = true;
  });
  for (var id in lastGood) {
    if (!kept[id]) delete lastGood[id];
  }
  for (var sid in status) {
    if (!kept[sid]) delete status[sid];
  }
  lastSent = [];
  sendSettings();
  poll({ fresh: true });
  schedule();
});
