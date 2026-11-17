# P21 footer anchor correction

The original P21 proposal placed the footer title center at `y=1058` (`positionY=0.9796296296`). That value copied the source CSS bottom edge directly, although native `titlePositionY` anchors the text block center. The footer copy therefore needed a separate center calculation.

In the existing Remotion source frame at 199.5 s, both footer strings have visible light-ink bounds at rows 1040–1051. The landscape footer uses `bottom:22px`, 17 px text, and `Looksy Mono`; CSS does not specify `line-height`. The Remotion font file and the app's embedded Space Mono file have identical SHA-256 `95837e182baeeada83368f7748db28357f0a1b75c6b84ff7065b5edf933c8e18`.

Qt, with that exact app font registered and the title font set to 17 px, reports `lineSpacing=25.15625`, ascent `19.03125`, and descent `6.125`. A source-derived line box extending upward from y=1058 therefore has center `y=1045.421875`, or `positionY=0.967983217592593`. The native title renderer uses a 20.4 px fixed advance for these title values and a 26 px text layout height after its Qt metric adjustment. The corrected anchor centers that block near the measured source glyphs.

The revised, separate proposal changes only the two new footer title anchors:

- Left: G50 (`graphics-d77585ff-bc9d-42e5-9908-a2494476c9b1`, lane index 100049).
- Right: G51 (`graphics-8e70fbee-995f-47ca-b8aa-644b39cbe9f3`, lane index 100050).

I previously called those lanes G75/G76 by confusing their `laneIndex` values with UI display labels. The corrected fragment carries the stable IDs, display labels, and lane indexes as separate guard values. The original P21 fragment and the root's admitted plan were not changed. Native A/B render review remains necessary before accepting this estimate.

The evidence and repeatable probes are in [p21-footer-anchor-v2-evidence.json](p21-footer-anchor-v2-evidence.json), [prepare_p21_footer_anchor_v2.py](prepare_p21_footer_anchor_v2.py), and [p21_footer_qt_metrics.cpp](p21_footer_qt_metrics.cpp). The corrected command fragment is [p21-film-caption-layout-v2-footer-anchor.commands-fragment.json](p21-film-caption-layout-v2-footer-anchor.commands-fragment.json).
