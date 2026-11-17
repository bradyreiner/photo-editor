const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
const source = fs.readFileSync(path.join(__dirname, '../site/product-demos.js'), 'utf8');

function mount({ reduced = false, narrow = false, visible = true, comparison = false } = {}) {
  const observers = [];
  const frames = new Map();
  let nextFrame = 0;
  let now = 0;
  function element() {
    const listeners = new Map();
    const attrs = new Map();
    return { dataset: {}, style: { setProperty(key, value) { this[key] = value; } },
      addEventListener(type, fn) { if (!listeners.has(type)) listeners.set(type, []); listeners.get(type).push(fn); },
      fire(type) { for (const fn of listeners.get(type) || []) fn(); },
      getAttribute: key => attrs.get(key), setAttribute: (key, value) => attrs.set(key, value),
      hasAttribute: key => attrs.has(key), querySelector: () => null, querySelectorAll: () => [] };
  }
  const document = Object.assign(element(), { hidden: false });
  const media = Object.assign(element(), { matches: reduced });
  const size = Object.assign(element(), { matches: narrow });
  const page = element();
  let isCurrent = visible;
  page.classList = { contains: () => isCurrent };
  const tools = [Object.assign(element(), { open: true })];
  const range = Object.assign(element(), { value: 50 });
  const stage = element();
  stage.querySelector = () => range;
  const videos = Array.from({ length: 2 }, (_, i) => {
    const video = Object.assign(element(), { paused: true, duration: 8, currentTime: 0, dataset: { src: `demo-${i}.mp4` }, load() { this.loaded = true; } });
    Object.defineProperty(video, 'src', { set(value) { this.setAttribute('src', value); } });
    video.play = () => { video.paused = false; video.fire('play'); return Promise.resolve(); };
    video.pause = () => { video.paused = true; video.fire('pause'); };
    return video;
  });
  const reels = videos.map((video, i) => {
    const reel = element();
    reel.querySelector = selector => selector === 'video' ? (comparison && i === 0 ? null : video) : selector === '[data-comparison]' && comparison && i === 0 ? stage : null;
    return reel;
  });
  const choices = [element(), element()];
  const controls = Object.fromEntries(['play', 'progress', 'time', 'open'].map(key => [key, element()]));
  const story = element();
  story.closest = () => page;
  story.setAttribute('data-rotate', '');
  story.querySelectorAll = selector => selector === '[data-reel]' ? reels : choices;
  story.querySelector = selector => controls[selector.match(/data-demo-(\w+)/)[1]];
  document.querySelectorAll = selector => selector === '.tool-details' ? tools : [story];
  const window = { matchMedia: query => query.includes('reduced') ? media : size,
    requestAnimationFrame: fn => { frames.set(++nextFrame, fn); return nextFrame; },
    cancelAnimationFrame: id => frames.delete(id) };
  class MutationObserver { constructor(fn) { observers.push(fn); } observe() {} }
  vm.runInNewContext(source, { document, window, performance: { now: () => now }, MutationObserver });
  return { videos, choices, controls, document, media, size, tools, stage, range,
    visible(value) { isCurrent = value; observers.forEach(fn => fn()); },
    frame() { now += 500; const pending = [...frames.values()]; frames.clear(); pending.forEach(fn => fn(now)); } };
}

test('only the active demo loads and plays; hidden sections pause', () => {
  const app = mount({ visible: false });
  assert.equal(app.videos[0].loaded, undefined);
  app.visible(true);
  assert.equal(app.videos[0].paused, false);
  assert.equal(app.videos[1].loaded, undefined);
  app.choices[1].fire('click');
  assert.equal(app.videos[0].paused, true);
  assert.equal(app.videos[1].paused, false);
  app.visible(false);
  assert.equal(app.videos[1].paused, true);
});

test('reduced motion waits for explicit play and responds to preference changes', () => {
  const app = mount({ reduced: true });
  assert.equal(app.videos[0].paused, true);
  app.controls.play.fire('click');
  assert.equal(app.videos[0].paused, false);
  app.media.fire('change');
  assert.equal(app.videos[0].paused, true);
});

test('manual pause survives section changes and document visibility changes', () => {
  const app = mount();
  app.controls.play.fire('click');
  app.visible(false); app.visible(true);
  assert.equal(app.videos[0].paused, true);
  app.controls.play.fire('click');
  app.document.hidden = true; app.document.fire('visibilitychange');
  assert.equal(app.videos[0].paused, true);
  app.document.hidden = false; app.document.fire('visibilitychange');
  assert.equal(app.videos[0].paused, false);
});

test('hero rotates after a completed example and reports actual playback progress', () => {
  const app = mount();
  app.videos[0].currentTime = 4; app.videos[0].fire('timeupdate');
  assert.equal(app.controls.progress.value, 0.5);
  assert.equal(app.controls.time.textContent, '00:04');
  app.videos[0].fire('ended');
  assert.equal(app.videos[1].paused, false);
  assert.equal(app.choices[1].getAttribute('aria-pressed'), 'true');
});

test('comparison animation yields to the user and preserves their chosen reveal', () => {
  const app = mount({ comparison: true });
  app.frame();
  assert.notEqual(Number(app.range.value), 50);
  app.range.value = 75; app.range.fire('input');
  assert.equal(app.stage.style['--split'], '75%');
  app.frame();
  assert.equal(app.range.value, 75);
  app.visible(false); app.visible(true);
  assert.equal(app.range.value, 75);
});

test('mobile tool disclosures stay available without crowding the demo', () => {
  const app = mount({ narrow: true });
  assert.equal(app.tools[0].open, false);
  app.size.matches = false; app.size.fire('change');
  assert.equal(app.tools[0].open, true);
});
