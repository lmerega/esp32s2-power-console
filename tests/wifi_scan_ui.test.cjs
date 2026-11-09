const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

// Run the actual embedded dashboard function, without contacting a device.
const sketch = fs.readFileSync(path.join(__dirname, '..', 'ESP32S2_WiFiManager_WebServer.ino'), 'utf8');
const start = sketch.indexOf('let wifiScanBusy = false;');
const end = sketch.indexOf('\n    function updateStatusView', start);
assert.ok(start >= 0 && end > start, 'embedded scan UI function must be present');
const source = sketch.slice(start, end);

function harness(responses, { fireDeadlines = false } = {}) {
  let clock = 0;
  const calls = [];
  const info = { textContent: '' };
  const select = { innerHTML: '', options: [], appendChild(option) { this.options.push(option); } };
  const button = { disabled: false };
  const elements = { wifiScanInfo: info, wifiSelect: select, wifiScanBtn: button };
  const context = vm.createContext({
    AbortController,
    document: { getElementById: id => elements[id], createElement: () => ({}) },
    setTimeout(callback, delay) {
      const timer = { cancelled: false };
      if (delay === 4000 || fireDeadlines) {
        queueMicrotask(() => {
          if (!timer.cancelled) { clock += delay; callback(); }
        });
      }
      return timer;
    },
    clearTimeout(timer) { timer.cancelled = true; },
    fetch(url, options) {
      calls.push({ url, at: clock });
      const response = responses.shift();
      assert.ok(response, 'unexpected additional scan request');
      if (typeof response === 'function') return response(options);
      return Promise.resolve({ ok: response.status === 200, status: response.status, json: async () => response.body });
    },
  });
  vm.runInContext(source, context);
  return { run: () => context.scanWifiNetworks(), calls, info, select, button };
}

const scanning = () => ({ status: 200, body: { ok: true, scanning: true } });
const success = () => ({ status: 200, body: { ok: true, count: 1, networks: [{ ssid: 'test-network', rssi: -50, signal_pct: 100, open: false }] } });

test('polls within API limits and renders successful scan results', async () => {
  const h = harness([scanning(), scanning(), success()]);
  await h.run();
  assert.deepEqual(h.calls.map(c => c.at), [0, 4000, 8000]);
  assert.equal(h.info.textContent, '1 reti trovate');
  assert.equal(h.select.options[0].value, 'test-network');
  assert.equal(h.button.disabled, false);
});

test('HTTP 429 is retried instead of being shown as zero networks', async () => {
  const h = harness([{ status: 429, body: { ok: false, error: 'scan_rate_limited' } }, success()]);
  await h.run();
  assert.equal(h.calls.length, 2);
  assert.equal(h.info.textContent, '1 reti trovate');
});

test('device scan errors are visible and release the scan button', async () => {
  const h = harness([{ status: 200, body: { ok: false, error: 'scan_timeout' } }]);
  await h.run();
  assert.equal(h.info.textContent, 'Scan KO: scan_timeout');
  assert.equal(h.button.disabled, false);
});

test('a nonresponsive HTTP request is aborted and releases the UI', async () => {
  const h = harness([({ signal }) => new Promise((resolve, reject) => {
    signal.addEventListener('abort', () => {
      const error = new Error('aborted');
      error.name = 'AbortError';
      reject(error);
    });
  })], { fireDeadlines: true });
  await h.run();
  assert.equal(h.info.textContent, 'Scan KO: dispositivo non risponde');
  assert.equal(h.button.disabled, false);
});

test('repeated clicks do not start concurrent scans', async () => {
  let finish;
  const h = harness([() => new Promise(resolve => { finish = resolve; })]);
  const first = h.run();
  await h.run();
  assert.equal(h.calls.length, 1);
  finish({ ok: true, status: 200, json: async () => success().body });
  await first;
  assert.equal(h.button.disabled, false);
});

test('endless scanning responses have a bounded polling budget', async () => {
  const h = harness(Array.from({ length: 8 }, scanning));
  await h.run();
  assert.equal(h.calls.length, 8);
  assert.equal(h.info.textContent, 'Scan timeout');
  assert.equal(h.button.disabled, false);
});
