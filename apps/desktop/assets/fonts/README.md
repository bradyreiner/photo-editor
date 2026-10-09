# Looksy creative fonts

These unmodified fonts are bundled for offline use in photo text layers and
Carousel designs. They are registered as application fonts at desktop startup;
they are not installed into the operating system or used to change the app UI.

| Family | Character |
| --- | --- |
| Space Grotesk | Technical, geometric headings |
| Space Mono | Monospaced captions and field notes |
| DM Sans | Clean editorial text |
| Cormorant Garamond | Expressive editorial serif |
| Bebas Neue | Tall condensed poster titles |
| Archivo Black | Heavy graphic headlines |
| Fraunces | Warm, expressive display serif |
| Outfit | Minimal geometric sans |

The source is the [official Google Fonts repository](https://github.com/google/fonts)
at revision `7085eb89a950e85db5b166b7a58d414544b4140c`.
Each family directory contains its original SIL Open Font License 1.1 and
copyright notices. Font files and licenses are embedded together in the desktop
executable. `manifest.json` records the exact upstream URLs and SHA-256 hashes
of every font and license; `creative_fonts_test` verifies these embedded bytes,
the licenses, and Qt family/glyph discovery.

Any future font replacement must preserve its license and notices, update the
manifest, and pass the resource test. The bundled font assets remain governed
by their accompanying licenses.
