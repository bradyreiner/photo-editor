const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const vm = require('node:vm');
const source = readFileSync(require('node:path').join(__dirname, '../site/support-page.js'), 'utf8');
function render(config) {
  const nodes = Object.fromEntries(['support-email', 'manage-subscription'].map(id => [id, {
    href: '#', disabled: true, removeAttribute(name) { if (name === 'aria-disabled') this.disabled = false; },
  }]));
  vm.runInNewContext(source, { URL, window: { LooksyDownloads: config }, document: { getElementById: id => nodes[id] } });
  return nodes;
}
test('billing management remains usable independently of support-email configuration', () => {
  const nodes = render({ billingPortalUrl: 'https://store.test/billing' });
  assert.equal(nodes['manage-subscription'].href, 'https://store.test/billing');
  assert.equal(nodes['manage-subscription'].disabled, false);
  assert.equal(nodes['support-email'].disabled, true);
});
test('missing and credential-bearing or non-HTTPS portal links stay disabled', () => {
  for (const billingPortalUrl of ['', 'http://store.test/billing', 'javascript:alert(1)', 'https://user:password@store.test/billing']) {
    const nodes = render({ billingPortalUrl });
    assert.equal(nodes['manage-subscription'].disabled, true);
    assert.equal(nodes['manage-subscription'].href, '#');
  }
});
