const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const source = fs.readFileSync(path.join(__dirname, '../site/download-page.js'), 'utf8');

function render(config) {
  const elements = new Map();
  function element(id) {
    if (!elements.has(id)) {
      const classes = new Set(['disabled']);
      const attributes = new Map([['aria-disabled', 'true']]);
      elements.set(id, { hidden: false, textContent: 'Download', href: '#', dataset: {},
        classList: { add: value => classes.add(value), remove: value => classes.delete(value),
          toggle: (value, enabled) => enabled ? classes.add(value) : classes.delete(value) },
        removeAttribute: key => attributes.delete(key), classes, attributes });
    }
    return elements.get(id);
  }
  const cards = ['mac-arm', 'mac-intel', 'windows'].map(platform => {
    const card = element(`${platform}-download`);
    card.dataset.platformCard = platform;
    return card;
  });
  const document = { body: element('body'), getElementById: element,
    querySelector: element,
    querySelectorAll: selector => selector === '[data-platform-card]' ? cards : [element('download-title')] };
  vm.runInNewContext(source, { window: { LooksyDownloads: config }, document, URL,
    navigator: { platform: 'MacIntel' }, Intl, Date,
    fetch: () => Promise.reject(new Error('offline fixture')) });
  return element;
}

const paid = { accessMode: 'paid-perpetual', supportedPlatforms: ['mac-arm'],
  commercialTermsApproved: true, priceLabel: 'Fixture price', licenseSummary: 'One user, current major version.',
  purchaseUrl: 'https://fixture.lemonsqueezy.com/buy/test',
  purchaseRecoveryUrl: 'https://app.lemonsqueezy.com/my-orders/' };

test('configured purchase and recovery links are enabled with the agreed rights', () => {
  const page = render(paid);
  assert.equal(page('purchase-button').href, paid.purchaseUrl);
  assert.equal(page('purchase-button').attributes.has('aria-disabled'), false);
  assert.match(page('purchase-button').textContent, /Fixture price/);
  assert.equal(page('purchase-recovery').href, paid.purchaseRecoveryUrl);
  assert.equal(page('commerce-description').textContent, paid.licenseSummary);
});

test('both purchase models have separate checkout choices and expiry terms', () => {
  const config = { ...paid, accessMode: 'paid-both', subscriptionPriceLabel: 'Fixture per month',
    subscriptionPurchaseUrl: 'https://fixture.lemonsqueezy.com/buy/monthly' };
  const page = render(config);
  assert.equal(page('subscription-button').hidden, false);
  assert.equal(page('subscription-button').href, config.subscriptionPurchaseUrl);
  assert.equal(page('purchase-button').href, paid.purchaseUrl);
  assert.match(page('commerce-detail').textContent, /view, save, and export/);
  const incomplete = render({ ...config, subscriptionPriceLabel: '' });
  assert.equal(incomplete('purchase-button').href, '#');
  assert.equal(incomplete('subscription-button').href, '#');
});

test('missing price, review or recovery keeps purchases unavailable', () => {
  for (const field of ['priceLabel', 'commercialTermsApproved', 'purchaseRecoveryUrl']) {
    const config = { ...paid, [field]: '' };
    const page = render(config);
    assert.equal(page('purchase-button').href, '#');
    assert.equal(page('purchase-button').attributes.get('aria-disabled'), 'true');
    assert.equal(page('purchase-button').textContent, 'Purchases opening soon');
  }
});

test('unsafe links cannot become checkout or download links', () => {
  for (const url of ['javascript:alert(1)', 'http://insecure.example', 'https://secret@example.com']) {
    const page = render({ ...paid, purchaseUrl: url, directDownloads: { macArm: url } });
    assert.equal(page('purchase-button').href, '#');
    assert.equal(page('mac-arm-download').href, '#');
  }
});

test('Apple silicon launch does not offer Intel or Windows installers', () => {
  const page = render({ ...paid, directDownloads: {
    macArm: 'https://example.com/arm.dmg', macIntel: 'https://example.com/intel.dmg', windows: 'https://example.com/win.msix' } });
  assert.equal(page('mac-arm-download').hidden, false);
  assert.equal(page('mac-intel-download').hidden, true);
  assert.equal(page('windows-download').hidden, true);
  assert.equal(page('mac-intel-download').href, '#');
  assert.equal(page('windows-requirement').hidden, true);
  assert.equal(page('body').classes.has('platform-mac'), false, 'Mac user agent cannot identify CPU architecture');
});

test('support payments do not expose a paid commercial license', () => {
  const page = render({ ...paid, accessMode: 'free-support' });
  assert.equal(page('purchase-button').hidden, true);
  assert.equal(page('purchase-button').href, '#');
});
