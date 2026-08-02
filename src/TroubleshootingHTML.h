#pragma once

// Troubleshooting page served at GET /troubleshooting
// Runs connectivity and hardware checks, displays scanner device ID prominently.

const char TROUBLESHOOTING_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Troubleshooting &mdash; SpoolSense</title>
  <link rel="stylesheet" href="/css/shared.css?v=)rawliteral" FIRMWARE_VERSION R"rawliteral(" />
  <style>
    .device-id-box {
      background: rgba(99,102,241,.12);
      border: 1px solid rgba(99,102,241,.35);
      border-radius: 14px;
      padding: 18px 22px;
      margin-bottom: 24px;
      text-align: center;
    }
    .device-id-label { font-size: 12px; color: var(--muted); text-transform: uppercase; letter-spacing: .08em; margin-bottom: 6px; }
    .device-id-value {
      font-size: 28px; font-weight: 800; letter-spacing: .12em;
      font-family: monospace; color: #a5b4fc;
    }
    .device-id-hint { font-size: 12px; color: var(--muted); margin-top: 6px; }
    .check-list { display: flex; flex-direction: column; gap: 10px; }
    .check-item {
      display: flex; align-items: center; gap: 14px;
      background: rgba(255,255,255,.03);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 14px 16px;
    }
    .check-icon { font-size: 20px; flex-shrink: 0; width: 24px; text-align: center; }
    .check-body { flex: 1; min-width: 0; }
    .check-name { font-size: 14px; font-weight: 700; color: #e5e7eb; }
    .check-detail { font-size: 12px; color: var(--muted); margin-top: 2px; }
    .check-item.pass  { border-color: rgba(74,222,128,.3); }
    .check-item.fail  { border-color: rgba(248,113,113,.3); }
    .check-item.warn  { border-color: rgba(251,191,36,.3);  }
    .check-item.loading { opacity: .6; }
    .run-btn {
      width: 100%; margin-top: 20px;
      padding: 13px; font-size: 15px; font-weight: 700;
      background: var(--accent); color: #fff;
      border: none; border-radius: 12px; cursor: pointer;
    }
    .run-btn:hover { opacity: .88; }
    .copy-btn {
      font-size: 11px; padding: 4px 10px;
      background: rgba(99,102,241,.2); color: #a5b4fc;
      border: 1px solid rgba(99,102,241,.4); border-radius: 6px;
      cursor: pointer; margin-top: 8px;
    }
    .copy-btn:hover { background: rgba(99,102,241,.35); }
  </style>
</head>
<body>
  <div class="wrap">
    <nav>
      <span class="nav-brand">SpoolSense</span>
      <a href="/">Home</a>
      <a href="/reader">Reader</a>
      <a href="/writer/openprinttag">OpenPrintTag</a>
      <a href="/writer/tigertag">TigerTag</a>
      <a href="/writer/opentag3d">OpenTag3D</a>
      <a href="/writer/openspool">OpenSpool</a>
      <a href="/register/uid">NFC+</a>
      <a href="/update">Update</a>
      <a href="/troubleshooting" class="active">Troubleshooting</a>
      <a href="/config">Config</a>
    </nav>

    <h2 style="margin-bottom:6px">Troubleshooting</h2>
    <p style="color:var(--muted);font-size:14px;margin-bottom:20px">
      Verify your scanner setup. Copy your Device ID for middleware configuration.
    </p>

    <div class="device-id-box">
      <div class="device-id-label">Scanner Device ID</div>
      <div class="device-id-value" id="deviceId">—</div>
      <div class="device-id-hint">Use this ID in your SpoolSense middleware config</div>
      <br>
      <button class="copy-btn" onclick="copyDeviceId()">Copy ID</button>
    </div>

    <div class="check-list" id="checkList">
      <div class="check-item loading" id="chk-wifi">
        <div class="check-icon">⟳</div>
        <div class="check-body">
          <div class="check-name">WiFi</div>
          <div class="check-detail">Checking...</div>
        </div>
      </div>
      <div class="check-item loading" id="chk-mqtt">
        <div class="check-icon">⟳</div>
        <div class="check-body">
          <div class="check-name">MQTT Broker</div>
          <div class="check-detail">Checking...</div>
        </div>
      </div>
      <div class="check-item loading" id="chk-spoolman">
        <div class="check-icon">⟳</div>
        <div class="check-body">
          <div class="check-name">Spoolman</div>
          <div class="check-detail">Checking...</div>
        </div>
      </div>
      <div class="check-item loading" id="chk-nfc">
        <div class="check-icon">⟳</div>
        <div class="check-body">
          <div class="check-name">NFC Reader (PN5180)</div>
          <div class="check-detail">Checking...</div>
        </div>
      </div>
      <div class="check-item loading" id="chk-heap">
        <div class="check-icon">⟳</div>
        <div class="check-body">
          <div class="check-name">Memory</div>
          <div class="check-detail">Checking...</div>
        </div>
      </div>
    </div>

    <button class="run-btn" id="runBtn" onclick="runChecks()">Run Checks</button>

    <div class="selftest">
      <h3 style="margin:28px 0 4px">Full Self-Test</h3>
      <p style="color:var(--muted);font-size:13px;margin-bottom:12px">
        Deeper, guided diagnostics with plain-language fixes and a sanitized report you can paste into a GitHub issue. Read-only — it never writes a tag or changes settings.
      </p>
      <label style="display:block;font-size:13px;margin:5px 0">
        <input type="checkbox" id="stOptNetwork" checked> Network checks (WiFi / MQTT / Spoolman / printer)
      </label>
      <label style="display:block;font-size:13px;margin:5px 0">
        <input type="checkbox" id="stOptStability" checked> NFC stability test <span style="color:var(--muted)">(place a tag on the reader when prompted)</span>
      </label>
      <button class="run-btn" id="stRunBtn" onclick="startSelfTest()">Run Self-Test</button>
      <button class="copy-btn" id="stCancelBtn" style="display:none;margin-top:10px" onclick="cancelSelfTest()">Cancel</button>

      <div id="stOverall" style="display:none;margin-top:14px;font-weight:700"></div>
      <div class="check-list" id="stResults" style="margin-top:12px"></div>

      <div id="stReportWrap" style="display:none;margin-top:14px">
        <button class="copy-btn" id="stCopyBtn" onclick="copyReport()">Copy report for GitHub</button>
        <pre id="stReport" style="white-space:pre-wrap;font-size:11px;background:rgba(0,0,0,.25);border-radius:8px;padding:10px;margin-top:8px;overflow-x:auto"></pre>
      </div>
    </div>

    <p style="margin-top:20px;font-size:13px;color:var(--muted)">
      Need more detail? <a href="/logs" style="color:var(--blue);font-weight:600">View Serial Log</a> for live scanner output.
    </p>
  </div>

  <div id="stModal" style="display:none;position:fixed;inset:0;z-index:1000;background:rgba(0,0,0,.72);align-items:center;justify-content:center">
    <div style="background:#15161c;border:1px solid var(--accent);border-radius:16px;padding:26px 24px;max-width:340px;margin:20px;text-align:center;box-shadow:0 14px 44px rgba(0,0,0,.55)">
      <div style="font-size:40px;margin-bottom:6px">🏷️</div>
      <div id="stModalText" style="font-size:15px;line-height:1.5;margin-bottom:20px">Place a tag on the reader.</div>
      <button class="run-btn" style="margin-top:0" onclick="selfTestContinue()">Continue</button>
      <button class="copy-btn" style="display:block;margin:12px auto 0" onclick="cancelSelfTest()">Cancel test</button>
    </div>
  </div>

  <script>
    function setCheck(id, status, name, detail) {
      const el = document.getElementById('chk-' + id);
      el.className = 'check-item ' + status;
      const icons = { pass: '✓', fail: '✗', warn: '⚠' };
      el.innerHTML =
        '<div class="check-icon">' + (icons[status] || '⟳') + '</div>' +
        '<div class="check-body">' +
          '<div class="check-name">' + name + '</div>' +
          '<div class="check-detail">' + detail + '</div>' +
        '</div>';
    }

    function signalLabel(rssi) {
      if (rssi >= -50) return 'Excellent';
      if (rssi >= -65) return 'Good';
      if (rssi >= -75) return 'Fair';
      return 'Weak';
    }

    function formatBytes(b) {
      return (b / 1024).toFixed(1) + ' KB';
    }

    function copyDeviceId() {
      const id = document.getElementById('deviceId').textContent;
      if (!id || id === '—') return;
      const btn = document.querySelector('.copy-btn');
      function flash(msg) {
        if (!btn) return;
        btn.textContent = msg;
        setTimeout(function(){ btn.textContent = 'Copy ID'; }, 1500);
      }
      // Clipboard API is secure-context only; the scanner is plain HTTP, so
      // fall back to a hidden textarea + execCommand.
      function legacyCopy() {
        try {
          var ta = document.createElement('textarea');
          ta.value = id;
          ta.style.position = 'fixed';
          ta.style.opacity = '0';
          document.body.appendChild(ta);
          ta.focus(); ta.select();
          var ok = document.execCommand('copy');
          document.body.removeChild(ta);
          if (ok) {
            flash('Copied!');
          } else {
            // Select the visible ID so a manual Ctrl/Cmd+C actually has a target
            var r = document.createRange();
            r.selectNodeContents(document.getElementById('deviceId'));
            var s = window.getSelection();
            s.removeAllRanges(); s.addRange(r);
            flash('Selected — press Ctrl/⌘+C');
          }
        } catch (e) {
          flash('Select the ID, then Ctrl/⌘+C');
        }
      }
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(id).then(function(){ flash('Copied!'); }, legacyCopy);
      } else {
        legacyCopy();
      }
    }

    async function runChecks() {
      const btn = document.getElementById('runBtn');
      btn.disabled = true;
      btn.textContent = 'Running...';

      // Reset all to loading
      ['wifi','mqtt','spoolman','nfc','heap'].forEach(id => {
        const el = document.getElementById('chk-' + id);
        el.className = 'check-item loading';
        el.querySelector('.check-detail').textContent = 'Checking...';
        el.querySelector('.check-icon').textContent = '⟳';
      });

      try {
        const r = await fetch('/api/diagnostics');
        const d = await r.json();

        // Device ID
        if (d.device_id) {
          document.getElementById('deviceId').textContent = d.device_id;
        }

        // WiFi
        if (d.wifi) {
          const w = d.wifi;
          if (w.connected) {
            const label = signalLabel(w.rssi_dbm);
            const status = w.rssi_dbm >= -75 ? 'pass' : 'warn';
            setCheck('wifi', status, 'WiFi',
              w.ssid + ' &mdash; ' + label + ' (' + w.rssi_dbm + ' dBm)');
          } else {
            setCheck('wifi', 'fail', 'WiFi', 'Not connected. Check SSID and password in Config.');
          }
        }

        // MQTT
        if (d.mqtt) {
          const m = d.mqtt;
          if (m.connected) {
            setCheck('mqtt', 'pass', 'MQTT Broker', 'Connected to ' + m.broker);
          } else if (!m.enabled) {
            setCheck('mqtt', 'warn', 'MQTT Broker', 'Disabled in config');
          } else {
            setCheck('mqtt', 'fail', 'MQTT Broker',
              'Cannot reach ' + m.broker + '. Check broker address and port in Config.');
          }
        }

        // Spoolman
        if (d.spoolman) {
          const s = d.spoolman;
          if (!s.enabled) {
            setCheck('spoolman', 'warn', 'Spoolman', 'Disabled in config');
          } else if (s.check_skipped) {
            setCheck('spoolman', 'warn', 'Spoolman',
              'Check skipped &mdash; the scanner was busy with another request. Refresh to retry.');
          } else if (s.reachable) {
            setCheck('spoolman', 'pass', 'Spoolman',
              'Connected &mdash; ' + s.url + (s.version ? ' (v' + s.version + ')' : ''));
          } else {
            setCheck('spoolman', 'fail', 'Spoolman',
              'Cannot reach ' + s.url + '. Check URL in Config.');
          }
        }

        // NFC
        if (d.nfc) {
          const n = d.nfc;
          if (n.ok) {
            setCheck('nfc', 'pass', 'NFC Reader',
              n.reader || 'Connected');
          } else {
            setCheck('nfc', 'fail', 'NFC Reader',
              'Not responding. Check SPI wiring.');
          }
        }

        // Heap
        if (d.memory) {
          const mem = d.memory;
          const status = mem.free_bytes > 50000 ? 'pass' : 'warn';
          const usedPct = mem.total_bytes > 0 ? Math.round(mem.used_bytes / mem.total_bytes * 100) : 0;
          setCheck('heap', status, 'Memory',
            'Free: ' + formatBytes(mem.free_bytes) +
            ' / Total: ' + formatBytes(mem.total_bytes) +
            ' (' + usedPct + '% used)' +
            ' &mdash; Uptime: ' + formatUptime(mem.uptime_s));
        }

      } catch(e) {
        const names = {wifi:'WiFi', mqtt:'MQTT Broker', spoolman:'Spoolman', nfc:'NFC Reader (PN5180)', heap:'Memory'};
        ['wifi','mqtt','spoolman','nfc','heap'].forEach(id => {
          setCheck(id, 'fail', names[id] || id, 'Error fetching diagnostics');
        });
      }

      btn.disabled = false;
      btn.textContent = 'Run Checks Again';
    }

    function formatUptime(s) {
      if (s < 60) return s + 's';
      if (s < 3600) return Math.floor(s/60) + 'm ' + (s%60) + 's';
      return Math.floor(s/3600) + 'h ' + Math.floor((s%3600)/60) + 'm';
    }

    // --- Full Self-Test wizard ---
    var stPollTimer = null;

    function stStatusClass(s) {
      if (s === 'PASS') return 'pass';
      if (s === 'FAIL') return 'fail';
      if (s === 'WARN') return 'warn';
      return '';
    }
    function stStatusIcon(s) {
      return {PASS:'✓', FAIL:'✗', WARN:'⚠', SKIP:'—'}[s] || '⟳';
    }
    function esc(t) {
      var d = document.createElement('div'); d.textContent = t == null ? '' : t; return d.innerHTML;
    }

    async function startSelfTest() {
      var btn = document.getElementById('stRunBtn');
      btn.disabled = true; btn.textContent = 'Running...';
      document.getElementById('stCancelBtn').style.display = 'inline-block';
      document.getElementById('stResults').innerHTML = '';
      document.getElementById('stReportWrap').style.display = 'none';
      document.getElementById('stOverall').style.display = 'none';
      var body = {
        network: document.getElementById('stOptNetwork').checked,
        stability: document.getElementById('stOptStability').checked
      };
      try {
        var r = await fetch('/api/diagnostics/session', {
          method: 'POST', headers: {'Content-Type': 'application/json'},
          body: JSON.stringify(body)
        });
        if (r.status === 409) { alert('A self-test is already running.'); }
      } catch (e) {}
      if (stPollTimer) clearInterval(stPollTimer);
      stPollTimer = setInterval(pollSelfTest, 700);
      pollSelfTest();
    }

    async function pollSelfTest() {
      var s;
      try { s = await (await fetch('/api/diagnostics/session')).json(); }
      catch (e) { return; }

      // Render results
      var html = '';
      (s.results || []).forEach(function(r) {
        html += '<div class="check-item ' + stStatusClass(r.status) + '">' +
                  '<div class="check-icon">' + stStatusIcon(r.status) + '</div>' +
                  '<div class="check-body">' +
                    '<div class="check-name">' + esc(r.test) + '</div>' +
                    '<div class="check-detail">' + esc(r.summary) +
                    (r.recommendation ? '<br><span style="color:var(--blue)">→ ' + esc(r.recommendation) + '</span>' : '') +
                    '</div></div></div>';
      });
      document.getElementById('stResults').innerHTML = html;

      // Waiting-for-user prompt — a centered modal so it can't be missed
      var modal = document.getElementById('stModal');
      if (s.waiting_for_user && s.prompt) {
        document.getElementById('stModalText').textContent = s.prompt;
        modal.style.display = 'flex';
      } else {
        modal.style.display = 'none';
      }

      if (!s.active) {
        clearInterval(stPollTimer); stPollTimer = null;
        modal.style.display = 'none';
        var btn = document.getElementById('stRunBtn');
        btn.disabled = false; btn.textContent = 'Run Self-Test Again';
        document.getElementById('stCancelBtn').style.display = 'none';
        var overall = document.getElementById('stOverall');
        overall.style.display = 'block';
        overall.textContent = 'Result: ' + s.overall +
          (s.stability_ran ? '  •  NFC stability score ' + s.stability_score + '/100' : '');
        loadReport();
      }
    }

    async function selfTestContinue() {
      document.getElementById('stModal').style.display = 'none';
      try { await fetch('/api/diagnostics/session/input', {method: 'POST'}); } catch (e) {}
    }

    async function cancelSelfTest() {
      try { await fetch('/api/diagnostics/session/cancel', {method: 'POST'}); } catch (e) {}
    }

    async function loadReport() {
      try {
        var txt = await (await fetch('/api/diagnostics/report')).text();
        document.getElementById('stReport').textContent = txt;
        document.getElementById('stReportWrap').style.display = 'block';
      } catch (e) {}
    }

    function copyReport() {
      var pre = document.getElementById('stReport');
      var btn = document.getElementById('stCopyBtn');
      function flash(msg) {
        if (!btn) return;
        btn.textContent = msg;
        setTimeout(function(){ btn.textContent = 'Copy report for GitHub'; }, 1800);
      }
      // Clipboard API needs a secure context (HTTPS/localhost); the scanner is
      // served over plain HTTP, so fall back to selection + execCommand.
      function legacyCopy() {
        try {
          var range = document.createRange();
          range.selectNodeContents(pre);
          var sel = window.getSelection();
          sel.removeAllRanges();
          sel.addRange(range);
          var ok = document.execCommand('copy');
          if (ok) {
            sel.removeAllRanges();
            flash('Copied!');
          } else {
            // Leave the report selected so the manual shortcut actually works
            flash('Selected — press Ctrl/⌘+C');
          }
        } catch (e) {
          flash('Select the text, then Ctrl/⌘+C');
        }
      }
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(pre.textContent).then(
          function(){ flash('Copied!'); }, legacyCopy);
      } else {
        legacyCopy();
      }
    }

    // Auto-run on page load
    window.addEventListener('load', runChecks);
  </script>
</body>
</html>
)rawliteral";
