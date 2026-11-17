const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const source = fs.readFileSync(path.join(__dirname, '../site/page-navigation.js'), 'utf8');

function mount({ hash = '', reduced = false } = {}) {
  let now = 0;
  let timerId = 0;
  const timers = new Map();
  const frames = [];
  function element(id) {
    const listeners = new Map();
    const attributes = new Map();
    const classes = new Set();
    return { id, dataset: {}, attributes, classes, scrollTop: 0, clientHeight: 600, scrollHeight: 600,
      addEventListener: (type, callback) => listeners.set(type, callback),
      dispatch(type, event = {}) { listeners.get(type)?.(event); },
      setAttribute: (key, value) => attributes.set(key, value),
      getAttribute: key => attributes.get(key), removeAttribute: key => attributes.delete(key),
      classList: { add: value => classes.add(value), toggle: (value, on) => on ? classes.add(value) : classes.delete(value) },
      closest(selector) { return selector === '[data-page]' && this.dataset.label ? this : null; },
      focus() { this.focused = true; } };
  }
  const document = element('document');
  const window = element('window');
  const deck = element('page-deck');
  const ids = ['overview', 'photos', 'design', 'download-page'];
  const pages = ids.map((id, i) => Object.assign(element(id), { offsetTop: i * 600, dataset: { label: id } }));
  const elements = new Map([['page-deck', deck], ...pages.map(p => [p.id, p])]);
  for (const id of ['previous-page', 'next-page', 'page-counter', 'page-select']) elements.set(id, element(id));
  const downloads = element('downloads');
  downloads.closest = () => pages[3];
  elements.set('downloads', downloads);
  document.getElementById = id => elements.get(id);
  document.querySelectorAll = () => [];
  document.body = element('body');
  deck.querySelectorAll = () => pages;
  deck.scrollTo = options => { deck.lastScroll = options; deck.scrollTop = options.top; };
  const media = Object.assign(element('media'), { matches: reduced });
  window.location = { hash };
  window.history = { replaceState: (_, __, value) => { window.location.hash = value; } };
  window.matchMedia = () => media;
  window.requestAnimationFrame = fn => { frames.push(fn); return frames.length; };
  window.setTimeout = fn => { timers.set(++timerId, fn); return timerId; };
  window.clearTimeout = id => timers.delete(id);
  vm.runInNewContext(source, { document, window, performance: { now: () => now } });
  const target = element('target');
  function event(values = {}) { return { target, preventDefault() { this.defaultPrevented = true; }, ...values }; }
  return { window, document, deck, pages, media, elements,
    wheel: values => deck.dispatch('wheel', event({ deltaY: 80, deltaX: 0, deltaMode: 0, ...values })),
    key: values => document.dispatch('keydown', event(values)),
    flushFrames() { while (frames.length) frames.shift()(); },
    settle() { now += 1000; for (const callback of timers.values()) callback(); timers.clear(); },
    active: () => pages.findIndex(p => !p.inert) };
}

test('a wheel gesture advances once, including its momentum, then accepts a new gesture', () => {
  const app = mount();
  app.wheel();
  assert.equal(app.active(), 1);
  for (let i = 0; i < 20; i++) app.wheel();
  assert.equal(app.active(), 1);
  app.settle();
  app.wheel();
  assert.equal(app.active(), 2);
  app.settle();
  app.wheel({ deltaY: -80 });
  assert.equal(app.active(), 1);
});

test('small deltas accumulate, while pinch and horizontal gestures do not change sections', () => {
  const app = mount();
  app.wheel({ ctrlKey: true });
  app.wheel({ deltaX: 100 });
  app.wheel({ deltaY: 10 });
  app.wheel({ deltaY: 10 });
  assert.equal(app.active(), 0);
  app.wheel({ deltaY: 10 });
  assert.equal(app.active(), 1);
});

test('overflow stays reachable before wheel or keyboard navigation leaves a section', () => {
  const app = mount();
  app.pages[0].scrollHeight = 900;
  app.wheel();
  app.key({ key: 'ArrowDown' });
  assert.equal(app.active(), 0);
  app.pages[0].scrollTop = 300;
  app.wheel();
  assert.equal(app.active(), 1);
});

test('deep links resolve nested downloads and reduced motion skips smooth scrolling', () => {
  const app = mount({ hash: '#downloads', reduced: true });
  assert.equal(app.active(), 3);
  assert.equal(app.elements.get('next-page').disabled, true);
  app.key({ key: 'Home' });
  assert.equal(app.active(), 0);
  assert.equal(app.deck.lastScroll.behavior, 'auto');
  assert.equal(app.elements.get('previous-page').disabled, true);
  app.key({ key: 'End' });
  assert.equal(app.active(), 3);
  assert.equal(app.window.location.hash, '#download-page');
});

test('keyboard shortcuts preserve form controls and section picker updates focus and state', () => {
  const app = mount();
  app.key({ key: 'ArrowDown', target: { closest: () => ({}) } });
  assert.equal(app.active(), 0);
  const picker = app.elements.get('page-select');
  picker.value = 'design';
  picker.dispatch('change');
  assert.equal(app.active(), 2);
  assert.equal(app.pages[2].focused, true);
  assert.equal(app.elements.get('page-counter').textContent, '03 / 04');
  assert.equal(app.pages[0].inert, true);
});

test('native touch snapping updates the active section and accessible navigation', () => {
  const app = mount();
  app.deck.dispatch('touchstart');
  app.deck.scrollTop = app.pages[2].offsetTop;
  app.deck.dispatch('scroll');
  app.flushFrames();
  assert.equal(app.active(), 2);
  assert.equal(app.window.location.hash, '#design');
  assert.equal(app.elements.get('page-select').value, 'design');
});
