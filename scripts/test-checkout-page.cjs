const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const os = require('node:os');
const { spawnSync } = require('node:child_process');
const { test } = require('node:test');

const source = fs.readFileSync(path.join(__dirname, '../site/checkout-page.js'), 'utf8');
const transaction = 'txn_' + 'a'.repeat(26);
const sandboxConfig = { environment: 'sandbox', clientToken: 'test_' + 'A'.repeat(27), checkoutOrigin: 'https://stage.looksy.test' };

function render(config = sandboxConfig, suffix = '?_ptxn=' + transaction, options = {}) {
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, { hidden: true, textContent: '', listeners: {},
      addEventListener(name, handler) { this.listeners[name] = handler; } });
    return elements.get(id);
  };
  const calls = [];
  const scripts = [];
  const location = new URL((options.origin || 'https://stage.looksy.test') + '/checkout.html' + suffix);
  let callback;
  const paddle = {
    Environment: { set: value => calls.push(['environment', value]) },
    Initialize(settings) {
      if (options.initThrows) throw new Error('SDK failed');
      calls.push(['initialize', settings]); callback = settings.eventCallback;
      assert.equal(location.search, '', 'query must be removed before SDK initialization to avoid automatic duplicate checkout');
    },
    Checkout: { open(settings) {
      calls.push(['open', settings]);
      if (options.openThrows) throw new Error('open failed');
    } }
  };
  const window = { LooksyPaddleCheckout: config, location, Paddle: paddle,
    history: { replaceState(_state, _title, target) {
      if (options.historyThrows) throw new Error('history unavailable');
      calls.push(['history', target]); location.href = new URL(target, location).href;
    } } };
  const document = { getElementById: element, createElement: () => ({}), head: { appendChild: script => scripts.push(script) } };
  vm.runInNewContext(source, { window, document, URL });
  return { element, scripts, calls, window, load() { scripts[0].onload(); }, event(name) { callback({ name }); } };
}

test('sandbox checkout opens exactly the authenticated-service transaction, with no client prices or customer overrides', () => {
  const page = render();
  assert.equal(page.scripts.length, 1);
  assert.equal(page.scripts[0].src, 'https://cdn.paddle.com/paddle/v2/paddle.js');
  assert.equal(page.scripts[0].referrerPolicy, 'no-referrer');
  page.load();
  assert.deepEqual(page.calls.map(call => call[0]), ['history', 'environment', 'initialize', 'open']);
  const opened = page.calls.at(-1)[1];
  assert.equal(opened.transactionId, transaction);
  assert.deepEqual(Object.keys(opened).sort(), ['settings', 'transactionId']);
  assert.equal(opened.settings.allowLogout, false);
  assert.equal(page.element('checkout-environment').hidden, false);
});

test('production uses only a live public client token and does not enable sandbox', () => {
  const page = render({ ...sandboxConfig, environment: 'production', clientToken: 'live_' + 'B'.repeat(27) });
  page.load();
  assert.equal(page.calls.some(call => call[0] === 'environment'), false);
  assert.equal(page.calls.find(call => call[0] === 'initialize')[1].token, 'live_' + 'B'.repeat(27));
  assert.equal(page.element('checkout-environment').hidden, true);
});

test('missing, malformed, duplicated or augmented transaction links never load the payment SDK', () => {
  for (const suffix of ['', '?price=pri_fake', '?_ptxn=txn_short', '?_ptxn=' + transaction + '&_ptxn=' + transaction,
    '?_ptxn=' + transaction + '&price=pri_fake', '?_ptxn=' + transaction + '&email=other@example.test',
    '?_ptxn=' + transaction + '#account', '?_ptxn=txn_' + 'A'.repeat(26)]) {
    const page = render(sandboxConfig, suffix);
    assert.equal(page.scripts.length, 0, suffix);
    assert.match(page.element('checkout-status').textContent, /Preferences → Account/);
  }
});

test('disabled configuration, mismatched environments and private-looking API keys fail closed', () => {
  for (const config of [undefined, {}, { ...sandboxConfig, clientToken: '' },
    { ...sandboxConfig, clientToken: 'pdl_sdbx_apikey_secret' },
    { ...sandboxConfig, clientToken: 'live_' + 'A'.repeat(27) },
    { ...sandboxConfig, clientToken: 'test_short' }, { ...sandboxConfig, environment: 'staging' },
    { ...sandboxConfig, checkoutOrigin: 'http://stage.looksy.test' },
    { ...sandboxConfig, checkoutOrigin: 'https://user@stage.looksy.test' },
    { ...sandboxConfig, checkoutOrigin: 'https://stage.looksy.test/checkout.html' },
    { ...sandboxConfig, checkoutOrigin: 'https://other.looksy.test' }]) {
    const page = render(config || {});
    assert.equal(page.scripts.length, 0);
    assert.match(page.element('checkout-status').textContent, /not configured/);
  }
  assert.equal(render(sandboxConfig, undefined, { origin: 'http://stage.looksy.test' }).scripts.length, 0);
});

test('blocked SDK and initialization failures show recovery without reflecting provider data', () => {
  const blocked = render(); blocked.scripts[0].onerror();
  assert.match(blocked.element('checkout-status').textContent, /could not load/);
  assert.equal(blocked.element('checkout-retry').hidden, true);
  const failed = render(sandboxConfig, undefined, { initThrows: true }); failed.load();
  assert.match(failed.element('checkout-status').textContent, /could not start/);
  assert.equal(failed.calls.some(call => call[0] === 'open'), false);
  assert.equal(render(sandboxConfig, undefined, { historyThrows: true }).scripts.length, 0);
});

test('closed or failed checkout can reopen the same transaction without reinitializing', () => {
  const page = render(); page.load(); page.event('checkout.closed');
  assert.equal(page.element('checkout-retry').hidden, false);
  page.element('checkout-retry').listeners.click();
  assert.equal(page.calls.filter(call => call[0] === 'initialize').length, 1);
  assert.equal(page.calls.filter(call => call[0] === 'open').length, 2);
  assert.equal(page.calls.at(-1)[1].transactionId, transaction);
  page.event('checkout.payment.failed');
  assert.match(page.element('checkout-status').textContent, /could not be completed/);
});

test('completion directs customers back to Looksy and never grants client-side entitlement or reopens checkout', () => {
  const page = render(); page.load(); page.event('checkout.completed');
  assert.match(page.element('checkout-status').textContent, /Return to Looksy.*refresh access.*select your license/);
  assert.equal(page.element('checkout-retry').hidden, true);
  page.event('checkout.closed'); page.element('checkout-retry').listeners.click();
  assert.match(page.element('checkout-status').textContent, /Checkout complete/);
  assert.equal(page.calls.filter(call => call[0] === 'open').length, 1);
});

test('an SDK open failure remains recoverable using the existing transaction', () => {
  const page = render(sandboxConfig, undefined, { openThrows: true }); page.load();
  assert.equal(page.element('checkout-retry').hidden, false);
  assert.match(page.element('checkout-status').textContent, /could not open/);
});

test('public configuration generator validates environment/token/origin and serializes no other environment values', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'looksy-checkout-test-'));
  try {
    const output = path.join(directory, 'public.js');
    const command = path.join(__dirname, 'configure-paddle-checkout.py');
    const valid = spawnSync('python3', [command, '--environment', 'sandbox', '--client-token', sandboxConfig.clientToken,
      '--checkout-origin', 'https://STAGE.looksy.test:443/', '--output', output], { encoding: 'utf8',
      env: { ...process.env, PADDLE_API_KEY: 'private-must-not-be-serialized' } });
    assert.equal(valid.status, 0, valid.stderr);
    const generated = fs.readFileSync(output, 'utf8');
    assert.match(generated, /https:\/\/stage.looksy.test/);
    assert.equal(generated.includes('private-must-not-be-serialized'), false);
    const invalid = spawnSync('python3', [command, '--environment', 'production', '--client-token', sandboxConfig.clientToken,
      '--checkout-origin', sandboxConfig.checkoutOrigin, '--output', output], { encoding: 'utf8' });
    assert.notEqual(invalid.status, 0);
    assert.equal(fs.readFileSync(output, 'utf8'), generated, 'invalid configuration must not overwrite a valid file');
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});

test('checkout accessibility, noindex and page-scoped CSP are present without weakening other pages', () => {
  const html = fs.readFileSync(path.join(__dirname, '../site/checkout.html'), 'utf8');
  const headers = fs.readFileSync(path.join(__dirname, '../site/_headers'), 'utf8');
  assert.match(html, /role="status" aria-live="polite"/);
  assert.match(html, /name="robots" content="noindex, nofollow"/);
  assert.match(html, /<noscript>/);
  assert.equal(html.includes('cdn.paddle.com'), false, 'SDK loads only after validating the transaction and public config');
  const checkoutHeaders = headers.split('/checkout.html\n')[1];
  assert.match(checkoutHeaders, /! Content-Security-Policy\n/);
  assert.match(checkoutHeaders, /script-src 'self' https:\/\/cdn.paddle.com/);
  assert.match(headers.split('/checkout.html\n')[0], /script-src 'self';/);
});

test('Pages clean checkout and legal URLs avoid canonical HTML rewrite loops', () => {
  const redirects = fs.readFileSync(path.join(__dirname, '../site/_redirects'), 'utf8');
  const rules = redirects.split(/\r?\n/).map(line => line.trim()).filter(line => line && !line.startsWith('#'));
  for (const alias of ['checkout', 'privacy', 'terms', 'refunds', 'support', 'licenses']) {
    assert.ok(fs.existsSync(path.join(__dirname, '../site', alias + '.html')));
    assert.ok(!rules.some(line => new RegExp('^/' + alias + '\\s+/' + alias + '\\.html\\s+200(?:\\s|$)').test(line)),
      alias + ' must use native Pages clean URL serving');
  }
});
