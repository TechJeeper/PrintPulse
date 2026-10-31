// Bambu Lab printers, two ways:
//
//  cloud  - signs in to the Bambu Lab account and reads every printer on it
//           from the (unofficial) cloud HTTP API. Live progress only travels
//           over MQTT, so progress/time may be estimated from the slicer's
//           prediction and the start time; those values are flagged.
//  bridge - reads tools/bambu_bridge.py on the local network, which relays the
//           printers' own LAN MQTT reports (works in LAN-only mode, exact data).

var http = require('../http');
var m = require('../model');

var HOSTS = { global: 'https://api.bambulab.com', china: 'https://api.bambulab.cn' };
var CLIENT_HEADERS = {
  'Content-Type': 'application/json',
  'Accept': 'application/json',
  'X-BBL-Client-Type': 'slicer',
  'X-BBL-Client-Name': 'OrcaSlicer',
  'X-BBL-Client-Version': '01.09.05.51',
  'X-BBL-OS-Type': 'linux',
  'X-BBL-Language': 'en'
};
var CANCELLED_BY_USER = 50348044;  // 0x0300400C

// ---- cloud ---------------------------------------------------------------

function call(src, method, path, body, token, cb) {
  var headers = {};
  for (var k in CLIENT_HEADERS) headers[k] = CLIENT_HEADERS[k];
  if (token) headers.Authorization = 'Bearer ' + token;
  http.request({
    method: method,
    url: (HOSTS[src.region] || HOSTS.global) + path,
    headers: headers,
    body: body,
    timeout: 12000
  }, function (err, res) {
    if (err) return cb(err);
    if (res.status === 403 && /cloudflare/i.test(res.text)) {
      return cb(new Error('Bambu Cloud busy, retrying'));
    }
    cb(null, res.status, res.json() || {});
  });
}

function sendCode(src) {
  call(src, 'POST', '/v1/user-service/user/sendemail/code', { email: src.email, type: 'codeLogin' }, null,
    function () {});
}

function signIn(src, ctx, cb) {
  var s = src.secrets || {};
  if (s.token) return cb(null, s.token);
  if (!src.email) return cb(m.setupError('Sign in from settings'));

  if (s.code) {
    var code = String(s.code).replace(/\s+/g, '');
    call(src, 'POST', '/v1/user-service/user/login', { account: src.email, code: code }, null,
      function (err, status, data) {
        if (err) return cb(err);
        if (data.accessToken) {
          ctx.update({ secrets: { token: data.accessToken, code: null, password: null }, needsCode: false });
          return cb(null, data.accessToken);
        }
        ctx.update({ secrets: { code: null }, needsCode: true });
        if (data.code === 1) {
          sendCode(src);
          return cb(m.setupError('Code expired, new one sent'));
        }
        cb(m.setupError('Wrong code, try again'));
      });
    return;
  }

  if (!s.password) {
    return cb(m.setupError(src.needsCode ? 'Enter the emailed code' : 'Sign in from settings'));
  }
  call(src, 'POST', '/v1/user-service/user/login', { account: src.email, password: s.password, apiError: '' }, null,
    function (err, status, data) {
      if (err) return cb(err);
      if (data.accessToken) {
        ctx.update({ secrets: { token: data.accessToken, password: null }, needsCode: false });
        return cb(null, data.accessToken);
      }
      // The password is never kept beyond this attempt.
      if (data.loginType === 'verifyCode') {
        sendCode(src);
        ctx.update({ secrets: { password: null }, needsCode: true });
        return cb(m.setupError('Enter the emailed code'));
      }
      ctx.update({ secrets: { password: null } });
      if (data.loginType === 'tfa') return cb(m.setupError('2FA on: use an access token'));
      cb(m.setupError(status === 400 || status === 401 ? 'Wrong email or password' : 'Sign-in failed'));
    });
}

function cloudState(status) {
  var s = String(status || '').toUpperCase();
  if (/RUNNING|PRINTING|ACTIVE/.test(s)) return m.STATE.PRINTING;
  if (/PAUSE/.test(s)) return m.STATE.PAUSED;
  if (/PREPARE|SLICING|INIT/.test(s)) return m.STATE.PREPARING;
  if (/FINISH|SUCCESS/.test(s)) return m.STATE.COMPLETE;
  if (/CANCEL/.test(s)) return m.STATE.CANCELLED;
  if (/FAIL/.test(s)) return m.STATE.ERROR;
  return m.STATE.IDLE;
}

function cloudPrinter(src, d, now) {
  var p = m.printer({
    uid: m.hash(src.id + ':' + d.id),
    name: d.name || 'Bambu Lab',
    kind: m.KIND.bambu,
    state: d.online ? cloudState(d.status) : m.STATE.OFFLINE,
    file: m.cleanFileName(d.task)
  });
  if (!d.online) {
    p.message = 'Printer is offline';
    return p;
  }
  var active = p.state === m.STATE.PRINTING || p.state === m.STATE.PAUSED || p.state === m.STATE.PREPARING;
  var start = d.start ? Date.parse(d.start) / 1000 : NaN;
  var elapsed = isFinite(start) ? Math.max(0, now - start) : null;
  var prediction = m.num(d.prediction);
  var reported = m.num(d.progress);
  if (active) {
    if (elapsed !== null) p.elapsed = Math.round(elapsed);
    if (reported !== null && reported > 0) {
      p.progress = m.permille(reported / 100);
    } else if (prediction > 0 && elapsed !== null) {
      p.progress = m.permille(Math.min(0.99, elapsed / prediction));
      p.flags |= m.FLAG.ESTIMATED;
    }
    if (prediction > 0 && elapsed !== null) {
      p.timeLeft = Math.max(0, Math.round(prediction - elapsed));
      p.flags |= m.FLAG.ESTIMATED;
    }
  } else {
    p.file = p.state === m.STATE.IDLE ? '' : p.file;
  }
  if (p.state === m.STATE.COMPLETE) p.progress = 1000;
  if (p.state === m.STATE.ERROR) p.message = 'Print failed';
  return p;
}

function listDevices(src, token, ctx, cb) {
  var unauthorized = function () {
    ctx.update({ secrets: { token: null } });
    cb(m.setupError('Sign in again in settings'));
  };
  call(src, 'GET', '/v1/iot-service/api/user/print?force=true', null, token, function (err, status, data) {
    if (err) return cb(err);
    if (status === 401) return unauthorized();
    if (status === 200 && data.devices) {
      return cb(null, data.devices.map(function (d) {
        return {
          id: d.dev_id, name: d.dev_name, online: d.dev_online !== false, status: d.task_status,
          task: d.task_name, start: d.start_time, prediction: d.prediction, progress: d.progress
        };
      }));
    }
    // Older accounts/regions: the bind list still has a coarse print status.
    call(src, 'GET', '/v1/iot-service/api/user/bind', null, token, function (err2, status2, data2) {
      if (err2) return cb(err2);
      if (status2 === 401) return unauthorized();
      if (status2 !== 200 || !data2.devices) return cb(new Error('Bambu Cloud error ' + status));
      cb(null, data2.devices.map(function (d) {
        return { id: d.dev_id, name: d.name, online: d.online !== false, status: d.print_status };
      }));
    });
  });
}

function fetchCloud(src, cb, ctx) {
  signIn(src, ctx, function (err, token) {
    if (err) return cb(err);
    listDevices(src, token, ctx, function (err2, devices) {
      if (err2) return cb(err2);
      ctx.setDevices(devices.map(function (d) { return { id: d.id, name: d.name }; }));
      var now = Date.now() / 1000;
      var hidden = src.hidden || [];
      cb(null, devices.filter(function (d) {
        return hidden.indexOf(d.id) < 0;
      }).map(function (d) {
        return cloudPrinter(src, d, now);
      }));
    });
  });
}

// ---- LAN bridge ----------------------------------------------------------

function errorCode(err) {
  var hi = ('0000' + (err >>> 16).toString(16)).slice(-4);
  var lo = ('0000' + (err & 0xffff).toString(16)).slice(-4);
  return (hi + '-' + lo).toUpperCase();
}

function lanPrinter(src, d) {
  var r = d.report || {};
  var p = m.printer({
    uid: m.hash(src.id + ':' + d.id),
    name: d.name || 'Bambu Lab',
    kind: m.KIND.bambu,
    stamp: m.num(d.updated) || 0
  });
  if (!d.online) {
    p.state = m.STATE.OFFLINE;
    p.message = d.error || 'Printer offline';
    return p;
  }
  var gs = String(r.gcode_state || '').toUpperCase();
  var err = m.num(r.print_error) || 0;
  switch (gs) {
    case 'RUNNING': p.state = m.STATE.PRINTING; break;
    case 'PREPARE':
    case 'SLICING':
    case 'INIT': p.state = m.STATE.PREPARING; break;
    case 'PAUSE': p.state = err ? m.STATE.ATTENTION : m.STATE.PAUSED; break;
    case 'FINISH': p.state = m.STATE.COMPLETE; break;
    case 'FAILED': p.state = err === CANCELLED_BY_USER ? m.STATE.CANCELLED : m.STATE.ERROR; break;
    case 'IDLE': p.state = m.STATE.IDLE; break;
    default: p.state = m.STATE.UNKNOWN;
  }
  p.nozzle = m.temp(r.nozzle_temper);
  p.nozzleTarget = Math.round(m.num(r.nozzle_target_temper) || 0);
  p.bed = m.temp(r.bed_temper);
  p.bedTarget = Math.round(m.num(r.bed_target_temper) || 0);
  if (p.state !== m.STATE.IDLE) {
    p.file = m.cleanFileName(r.subtask_name || r.gcode_file);
    var percent = m.num(r.mc_percent);
    if (percent !== null) p.progress = m.permille(percent / 100);
    if (p.state === m.STATE.COMPLETE) p.progress = 1000;
    if (m.num(r.layer_num) !== null) p.layer = r.layer_num;
    if (m.num(r.total_layer_num) > 0) p.layerTotal = r.total_layer_num;
  }
  if (p.state === m.STATE.PRINTING || p.state === m.STATE.PAUSED || p.state === m.STATE.ATTENTION ||
      p.state === m.STATE.PREPARING) {
    var minutes = m.num(r.mc_remaining_time);
    if (minutes !== null) p.timeLeft = Math.round(minutes * 60);
  }
  if (err && (p.state === m.STATE.ERROR || p.state === m.STATE.ATTENTION)) {
    p.message = 'Error ' + errorCode(err);
  }
  return p;
}

function fetchBridge(src, cb, ctx) {
  var token = src.secrets && src.secrets.token;
  http.getJson({
    url: m.baseUrl(src.host) + '/status',
    headers: token ? { 'X-Bridge-Token': token } : {}
  }, function (err, data) {
    if (err) return cb(err.status === 401 ? m.setupError('Bridge token is wrong') : err);
    var list = (data && data.printers) || [];
    ctx.setDevices(list.map(function (d) { return { id: d.id, name: d.name }; }));
    var hidden = src.hidden || [];
    cb(null, list.filter(function (d) {
      return hidden.indexOf(d.id) < 0;
    }).map(function (d) {
      return lanPrinter(src, d);
    }));
  });
}

module.exports = {
  fetch: function (src, cb, ctx) {
    (src.mode === 'bridge' ? fetchBridge : fetchCloud)(src, cb, ctx);
  },
  // Be gentle with Bambu's cloud; the local bridge can be polled freely.
  minInterval: function (src) {
    return src.mode === 'bridge' ? 0 : 60;
  },
  _cloudPrinter: cloudPrinter,
  _lanPrinter: lanPrinter
};
