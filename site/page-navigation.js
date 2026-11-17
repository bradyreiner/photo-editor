(function () {
  'use strict';

  const deck = document.getElementById('page-deck');
  if (!deck) return;
  const pages = [...deck.querySelectorAll('[data-page]')];
  if (!pages.length) return;
  const previous = document.getElementById('previous-page');
  const next = document.getElementById('next-page');
  const counter = document.getElementById('page-counter');
  const picker = document.getElementById('page-select');
  const navigation = [...document.querySelectorAll('.section-links a, .topnav a')];
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
  let current = 0;
  let requested = null;
  let motionUntil = 0;
  let scrollFrame = 0;
  let wheelTotal = 0;
  let wheelDirection = 0;
  let wheelCommitted = false;
  let wheelReset = 0;

  function targetPage(hash) {
    if (!hash || hash === '#') return null;
    let id;
    try { id = decodeURIComponent(hash.slice(1)); } catch (_) { return null; }
    const target = document.getElementById(id);
    return target?.closest('[data-page]') || null;
  }

  function showCurrent(index, writeHash = true) {
    current = index;
    pages.forEach((page, position) => {
      const selected = position === current;
      page.classList.toggle('is-current', selected);
      page.inert = !selected;
    });
    previous.disabled = current === 0;
    next.disabled = current === pages.length - 1;
    counter.textContent = `${String(current + 1).padStart(2, '0')} / ${String(pages.length).padStart(2, '0')}`;
    counter.setAttribute('aria-label', `${pages[current].dataset.label}, section ${current + 1} of ${pages.length}`);
    picker.value = pages[current].id;
    navigation.forEach((anchor) => {
      if (targetPage(anchor.getAttribute('href')) === pages[current]) anchor.setAttribute('aria-current', 'page');
      else anchor.removeAttribute('aria-current');
    });
    if (writeHash) window.history.replaceState(null, '', `#${pages[current].id}`);
  }

  function go(index, animate = true, focus = false, writeHash = true) {
    const destination = Math.max(0, Math.min(pages.length - 1, index));
    requested = destination;
    showCurrent(destination, writeHash);
    const smooth = animate && !reducedMotion.matches;
    motionUntil = performance.now() + (smooth ? 760 : 0);
    deck.scrollTo({ top: pages[destination].offsetTop, behavior: smooth ? 'smooth' : 'auto' });
    if (focus) pages[destination].focus({ preventScroll: true });
    if (!smooth) requested = null;
  }

  function syncScroll() {
    scrollFrame = 0;
    if (requested !== null) {
      if (Math.abs(deck.scrollTop - pages[requested].offsetTop) > 2) return;
      requested = null;
    }
    let closest = current;
    let distance = Infinity;
    pages.forEach((page, index) => {
      const delta = Math.abs(deck.scrollTop - page.offsetTop);
      if (delta < distance) { distance = delta; closest = index; }
    });
    if (closest !== current) showCurrent(closest);
  }

  deck.addEventListener('scroll', () => {
    if (!scrollFrame) scrollFrame = window.requestAnimationFrame(syncScroll);
  }, { passive: true });

  // A wheel gesture advances one screen. Trackpad momentum stays with that gesture.
  deck.addEventListener('wheel', (event) => {
    if (event.ctrlKey || Math.abs(event.deltaX) > Math.abs(event.deltaY) || !event.deltaY) return;
    if (event.target.closest('input, textarea, select, [contenteditable="true"]')) return;
    const page = pages[current];
    const direction = Math.sign(event.deltaY);
    const canScrollInside = direction > 0
      ? page.scrollTop + page.clientHeight < page.scrollHeight - 2
      : page.scrollTop > 2;
    // Keep enlarged text and open requirement details reachable on short screens.
    if (canScrollInside) return;
    event.preventDefault();
    window.clearTimeout(wheelReset);
    wheelReset = window.setTimeout(() => {
      wheelTotal = 0;
      wheelDirection = 0;
      wheelCommitted = false;
    }, 180);
    if (wheelCommitted || performance.now() < motionUntil) return;
    if (wheelDirection !== direction) wheelTotal = 0;
    wheelDirection = direction;
    const unit = event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? deck.clientHeight : 1;
    wheelTotal += Math.abs(event.deltaY) * unit;
    if (wheelTotal < 28) return;
    wheelCommitted = true;
    wheelTotal = 0;
    go(current + direction);
  }, { passive: false });

  // Touch uses the browser's native scroll snapping rather than a simulated swipe.
  deck.addEventListener('touchstart', () => { requested = null; }, { passive: true });

  document.addEventListener('keydown', (event) => {
    if (event.defaultPrevented || event.altKey || event.ctrlKey || event.metaKey) return;
    if (event.target.closest('input, textarea, select, button, [role="tablist"], [contenteditable="true"], summary')) return;
    // Preserve ordinary scrolling within a section when zoom makes it taller.
    const page = pages[current];
    const down = event.key === 'ArrowDown' || event.key === 'PageDown' || (event.key === ' ' && !event.shiftKey);
    const up = event.key === 'ArrowUp' || event.key === 'PageUp' || (event.key === ' ' && event.shiftKey);
    if (down && page.scrollTop + page.clientHeight < page.scrollHeight - 2) return;
    if (up && page.scrollTop > 2) return;
    let destination;
    if (down) destination = current + 1;
    if (up) destination = current - 1;
    if (event.key === 'Home') destination = 0;
    if (event.key === 'End') destination = pages.length - 1;
    if (destination === undefined) return;
    event.preventDefault();
    if (event.repeat && performance.now() < motionUntil) return;
    go(destination, true, true);
  });

  document.addEventListener('click', (event) => {
    if (event.defaultPrevented || event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
    const anchor = event.target.closest('a[href^="#"]');
    if (!anchor) return;
    const page = targetPage(anchor.getAttribute('href'));
    if (!page) return;
    event.preventDefault();
    go(pages.indexOf(page), true, true);
  });
  previous.addEventListener('click', () => go(current - 1, true, true));
  next.addEventListener('click', () => go(current + 1, true, true));
  picker.addEventListener('change', () => {
    const index = pages.findIndex((page) => page.id === picker.value);
    if (index >= 0) go(index, true, true);
  });
  window.addEventListener('hashchange', () => {
    const page = targetPage(window.location.hash);
    if (page) go(pages.indexOf(page), true, false, false);
  });
  window.addEventListener('resize', () => go(current, false));
  reducedMotion.addEventListener('change', () => go(current, false));

  document.body.classList.add('paged-site');
  const initial = targetPage(window.location.hash);
  go(initial ? pages.indexOf(initial) : 0, false, false, false);
})();
