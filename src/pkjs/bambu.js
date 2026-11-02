// Bambu Lab printers, two ways:
//
//  cloud  - signs in to the Bambu Lab account and reads every printer on it
//           from the (unofficial) cloud HTTP API. Device state comes from
//           /user/bind (print_status). Job name/progress come from /user/print
//           when present; otherwise /user-service/my/tasks supplies title,
//           start time and slicer estimate (endTime ≈ startTime ⇒ in progress).
//           Live temps/layers travel over MQTT only, so progress/time may be
//           estimated and flagged; use the LAN bridge for exact numbers.
//  bridge - reads tools/bambu_bridge.py on the local network, which relays the
//           printers' own LAN MQTT reports (works in LAN-only mode, exact data).

var http = require('./http');
var m = require('./model');

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
      if (data.loginType === 'tfa') return cb(m.setupError('2FA/social: paste an access token'));
      cb(m.setupError(status === 400 || status === 401 ? 'Wrong email or password' : 'Sign-in failed'));
    });
}

function cloudState(status) {
  var s = String(status || '').toUpperCase();
  if (/RUNNING|PRINTING|ACTIVE/.test(s)) return m.STATE.PRINTING;
  if (/PAUSE/.test(s)) return m.STATE.PAUSED;
  if (/PREPARE|SLICING|INIT/.test(s)) return m.STATE.PREPARING;
  if (/FINISH/.test(s)) return m.STATE.COMPLETE;
  // SUCCESS is Bambu's sticky "last job ok" / ready, not an in-progress finish screen.
  if (/SUCCESS/.test(s)) return m.STATE.IDLE;
  if (/CANCEL/.test(s)) return m.STATE.CANCELLED;
  if (/FAIL/.test(s)) return m.STATE.ERROR;
  return m.STATE.IDLE;
}

function cloudPrinter(src, d, now) {
  var reported = m.num(d.progress);
  var state = d.online ? cloudState(d.status) : m.STATE.OFFLINE;
  // Print endpoint still carrying a finished job → Complete (so the watch can buzz once).
  if (state === m.STATE.IDLE && /SUCCESS/i.test(String(d.status || '')) &&
      (d.task || (reported !== null && reported >= 100))) {
    state = m.STATE.COMPLETE;
  }
  // Job fields without a usable status still mean an active print.
  if (state === m.STATE.IDLE && d.task && reported !== null && reported > 0 && reported < 100) {
    state = m.STATE.PRINTING;
  }
  var p = m.printer({
    uid: m.hash(src.id + ':' + d.id),
    name: d.name || 'Bambu Lab',
    kind: m.KIND.bambu,
    state: state,
    file: m.cleanFileName(d.task)
  });
  p.deviceId = d.id;
  if (!d.online) {
    p.message = 'Printer is offline';
    return p;
  }
  var active = p.state === m.STATE.PRINTING || p.state === m.STATE.PAUSED || p.state === m.STATE.PREPARING;
  var start = d.start ? Date.parse(d.start) / 1000 : NaN;
  var elapsed = isFinite(start) ? Math.max(0, now - start) : null;
  var prediction = m.num(d.prediction);
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

// /user/print has progress and task name, but task_status is often null during a
// local/SD print. /user/bind's print_status (RUNNING/SUCCESS/…) stays accurate, so
// always merge both: bind for state, print for live job fields when present.
function mergeCloudDevices(printDevices, bindDevices) {
  var byId = {};
  var order = [];
  function ensure(id) {
    if (!byId[id]) {
      byId[id] = { id: id, name: '', online: true, status: '' };
      order.push(id);
    }
    return byId[id];
  }
  (bindDevices || []).forEach(function (d) {
    var cur = ensure(d.dev_id);
    cur.name = d.name || cur.name;
    cur.online = d.online !== false;
    cur.status = d.print_status || cur.status;
  });
  (printDevices || []).forEach(function (d) {
    var cur = ensure(d.dev_id);
    cur.name = d.dev_name || cur.name;
    if (d.dev_online !== undefined && d.dev_online !== null) {
      cur.online = d.dev_online !== false;
    }
    if (d.task_status) cur.status = d.task_status;
    // Keep prior values when print returns nulls for an active local/SD job.
    if (d.task_name) cur.task = d.task_name;
    if (d.start_time) cur.start = d.start_time;
    if (d.prediction != null && d.prediction !== '') cur.prediction = d.prediction;
    if (d.progress != null && d.progress !== '') cur.progress = d.progress;
  });
  return order.map(function (id) { return byId[id]; });
}

// In-progress tasks keep endTime within ~1 minute of startTime (Bambu placeholder).
function isActiveCloudTask(task) {
  var start = Date.parse(task && task.startTime);
  var end = Date.parse(task && task.endTime);
  return isFinite(start) && isFinite(end) && (end - start) <= 60000;
}

function needsCloudJobDetails(d) {
  if (!d || !d.online) return false;
  var s = String(d.status || '').toUpperCase();
  if (!/RUNNING|PRINTING|ACTIVE|PAUSE|PREPARE|SLICING|INIT/.test(s)) return false;
  return !d.task || !d.start || d.prediction == null || d.prediction === '';
}

// Fill gaps from /user-service/my/tasks when /user/print has no job row.
function applyCloudTasks(devices, hits) {
  var activeByDev = {};
  (hits || []).forEach(function (t) {
    if (!t || !t.deviceId || activeByDev[t.deviceId] || !isActiveCloudTask(t)) return;
    activeByDev[t.deviceId] = t;
  });
  (devices || []).forEach(function (d) {
    var t = activeByDev[d.id];
    if (!t) return;
    if (!d.task && t.title) d.task = t.title;
    if (!d.start && t.startTime) d.start = t.startTime;
    if ((d.prediction == null || d.prediction === '') && t.costTime != null) {
      d.prediction = t.costTime;
    }
  });
  return devices;
}

function listDevices(src, token, ctx, cb) {
  var unauthorized = function () {
    ctx.update({ secrets: { token: null } });
    cb(m.setupError('Sign in again in settings'));
  };
  call(src, 'GET', '/v1/iot-service/api/user/print?force=true', null, token, function (err, status, data) {
    if (err) return cb(err);
    if (status === 401) return unauthorized();
    var printDevices = (status === 200 && data && data.devices) ? data.devices : null;

    call(src, 'GET', '/v1/iot-service/api/user/bind', null, token, function (err2, status2, data2) {
      if (err2) return cb(err2);
      if (status2 === 401) return unauthorized();
      var bindDevices = (status2 === 200 && data2 && data2.devices) ? data2.devices : null;
      if (!printDevices && !bindDevices) {
        return cb(new Error('Bambu Cloud error ' + (status2 || status)));
      }
      var devices = mergeCloudDevices(printDevices, bindDevices);
      var missing = devices.some(needsCloudJobDetails);
      if (!missing) return cb(null, devices);

      call(src, 'GET', '/v1/user-service/my/tasks?limit=20', null, token, function (err3, status3, data3) {
        if (err3 || status3 === 401) {
          // Details are best-effort; status from bind is still useful.
          return cb(null, devices);
        }
        cb(null, applyCloudTasks(devices, (data3 && data3.hits) || []));
      });
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
  p.deviceId = d.id;
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

function controlBridge(src, deviceId, action, cb) {
  if (!deviceId) return cb(new Error('Unknown printer'));
  var token = src.secrets && src.secrets.token;
  http.request({
    method: 'POST',
    url: m.baseUrl(src.host) + '/command',
    headers: token ? { 'X-Bridge-Token': token, 'Content-Type': 'application/json' }
      : { 'Content-Type': 'application/json' },
    body: { id: deviceId, command: action }
  }, function (err, res) {
    if (err) return cb(err);
    if (res.status === 401) return cb(m.setupError('Bridge token is wrong'));
    if (res.status === 404) return cb(new Error('Printer not on bridge'));
    if (res.status < 200 || res.status >= 300) {
      var data = res.json() || {};
      return cb(new Error(data.error || ('HTTP ' + res.status)));
    }
    cb(null);
  });
}

function control(src, opts, action, cb) {
  if (src.mode === 'bridge') {
    return controlBridge(src, opts && opts.deviceId, action, cb);
  }
  // Cloud control needs MQTT, which the phone app can't open.
  cb(new Error('Bambu pause/stop needs the LAN bridge'));
}

module.exports = {
  fetch: function (src, cb, ctx) {
    (src.mode === 'bridge' ? fetchBridge : fetchCloud)(src, cb, ctx);
  },
  control: control,
  // Be gentle with Bambu's cloud; the local bridge can be polled freely.
  minInterval: function (src) {
    return src.mode === 'bridge' ? 0 : 60;
  },
  _cloudPrinter: cloudPrinter,
  _lanPrinter: lanPrinter,
  _mergeCloudDevices: mergeCloudDevices,
  _applyCloudTasks: applyCloudTasks,
  _isActiveCloudTask: isActiveCloudTask
};
