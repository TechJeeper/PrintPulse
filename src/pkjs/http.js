// XMLHttpRequest wrapper: timeouts, JSON, and HTTP digest auth (PrusaLink).

var md5 = require('./md5');

var DEFAULT_TIMEOUT = 8000;
var challenges = {};  // origin -> last digest challenge

function originOf(url) {
  var m = /^([a-z]+:\/\/[^\/]+)/i.exec(url);
  return m ? m[1].toLowerCase() : url;
}

function pathOf(url) {
  return url.replace(/^[a-z]+:\/\/[^\/]+/i, '') || '/';
}

function wrap(xhr) {
  return {
    status: xhr.status,
    text: xhr.responseText || '',
    header: function (name) {
      try {
        return xhr.getResponseHeader(name);
      } catch (e) {
        return null;
      }
    },
    json: function () {
      try {
        return JSON.parse(xhr.responseText);
      } catch (e) {
        return null;
      }
    }
  };
}

function send(opts, authorization, cb) {
  var xhr = new XMLHttpRequest();
  var finished = false;
  var timer = setTimeout(function () {
    if (finished) return;
    finished = true;
    try {
      xhr.abort();
    } catch (e) {}
    cb(new Error('Timed out'));
  }, opts.timeout || DEFAULT_TIMEOUT);

  xhr.onreadystatechange = function () {
    if (xhr.readyState !== 4 || finished) return;
    finished = true;
    clearTimeout(timer);
    if (xhr.status === 0) {
      cb(new Error("Can't connect"));
    } else {
      cb(null, wrap(xhr));
    }
  };

  try {
    xhr.open(opts.method || 'GET', opts.url, true);
    var headers = opts.headers || {};
    for (var name in headers) {
      if (headers.hasOwnProperty(name) && headers[name] !== undefined && headers[name] !== '') {
        try {
          xhr.setRequestHeader(name, headers[name]);
        } catch (e) {
          // Some runtimes refuse headers like User-Agent; carry on without them.
        }
      }
    }
    if (authorization) xhr.setRequestHeader('Authorization', authorization);
    var body = opts.body;
    xhr.send(body === undefined || body === null ? null : (typeof body === 'string' ? body : JSON.stringify(body)));
  } catch (e) {
    if (!finished) {
      finished = true;
      clearTimeout(timer);
      cb(new Error("Can't connect"));
    }
  }
}

function parseChallenge(header) {
  if (!header || !/^\s*digest\s/i.test(header)) return null;
  var out = { nc: 0 };
  var re = /(\w+)=("([^"]*)"|[^,\s]*)/g;
  var m;
  while ((m = re.exec(header))) {
    out[m[1].toLowerCase()] = m[3] !== undefined ? m[3] : m[2];
  }
  return out.nonce ? out : null;
}

function digestHeader(opts, ch) {
  var user = opts.digest.user;
  var method = opts.method || 'GET';
  var uri = pathOf(opts.url);
  var ha1 = md5(user + ':' + ch.realm + ':' + opts.digest.pass);
  var ha2 = md5(method + ':' + uri);
  var parts = [
    'username="' + user + '"',
    'realm="' + ch.realm + '"',
    'nonce="' + ch.nonce + '"',
    'uri="' + uri + '"'
  ];
  var response;
  if (ch.qop && /(^|,)\s*auth\s*(,|$)/.test(ch.qop)) {
    ch.nc += 1;
    var nc = ('0000000' + ch.nc.toString(16)).slice(-8);
    var cnonce = md5(String(Math.random()) + Date.now()).slice(0, 16);
    response = md5(ha1 + ':' + ch.nonce + ':' + nc + ':' + cnonce + ':auth:' + ha2);
    parts.push('qop=auth', 'nc=' + nc, 'cnonce="' + cnonce + '"');
  } else {
    response = md5(ha1 + ':' + ch.nonce + ':' + ha2);
  }
  parts.push('response="' + response + '"');
  if (ch.opaque) parts.push('opaque="' + ch.opaque + '"');
  if (ch.algorithm) parts.push('algorithm=' + ch.algorithm);
  return 'Digest ' + parts.join(', ');
}

// opts: { url, method, headers, body, timeout, digest: { user, pass } }
// cb(err, res) where res has status, text, json(), header(name)
function request(opts, cb) {
  if (!opts.digest) {
    send(opts, null, cb);
    return;
  }
  var origin = originOf(opts.url);
  var known = challenges[origin];
  send(opts, known ? digestHeader(opts, known) : null, function (err, res) {
    if (err || res.status !== 401) {
      cb(err, res);
      return;
    }
    var fresh = parseChallenge(res.header('WWW-Authenticate'));
    // Same nonce rejected twice means the password itself is wrong.
    if (!fresh || (known && known.nonce === fresh.nonce && !/true/i.test(fresh.stale || ''))) {
      cb(null, res);
      return;
    }
    challenges[origin] = fresh;
    send(opts, digestHeader(opts, fresh), cb);
  });
}

function getJson(opts, cb) {
  request(opts, function (err, res) {
    if (err) return cb(err);
    if (res.status === 401 || res.status === 403) return cb(httpError(res), null, res);
    if (res.status < 200 || res.status >= 300) return cb(httpError(res), null, res);
    var data = res.json();
    if (data === null && res.status !== 204) return cb(new Error('Unexpected reply'), null, res);
    cb(null, data, res);
  });
}

function httpError(res) {
  var e = new Error(res.status === 401 || res.status === 403 ? 'Access denied' : 'HTTP ' + res.status);
  e.status = res.status;
  return e;
}

module.exports = {
  request: request,
  getJson: getJson,
  httpError: httpError,
  _parseChallenge: parseChallenge,
  _digestHeader: digestHeader
};
