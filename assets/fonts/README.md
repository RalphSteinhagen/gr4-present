# Built-in faces

The viewer's text is Liberation Sans (regular, bold, italic, bold italic) and Liberation Mono, release
2.1.5, under SIL OFL 1.1 (`assets/liberation/LICENSE.txt`). Each covers Latin, Greek and Cyrillic, and
Liberation Mono keeps the widths of Cousine, the code face it replaces. The files are the release's
own, unmodified -- a subset would be a Modified Version, which the OFL forbids to carry the reserved
name -- and gzipped once, by hand, so the binary carries 1.05 MB rather than 1.97 MB:

```sh
gzip -9 -n -c LiberationSans-Regular.ttf > assets/liberation/LiberationSans-Regular.ttf.gz
```

and the same for `-Bold`, `-Italic`, `-BoldItalic` and `LiberationMono-Regular`. `-n` leaves out the
name and time stamp, so the output is reproducible. The viewer inflates them once at start with GR4's
`gr::compression::decompress`, and the PDF export embeds the same bytes.

OFL 1.1 is compatible with this project's GPL-3.0-or-later.

# Fonts carried from OpenDigitizer

`assets/fontawesome/` and `assets/xkcd/` mirror OpenDigitizer's `src/ui/assets/` exactly, licence files
included, so the terms travel with the files rather than living in another repository.

| file | licence | in the binary |
| --- | --- | --- |
| `fontawesome/fa-solid-900.otf` | SIL OFL 1.1, stated GPL-friendly | subset, for viewer icons |
| `fontawesome/fa-regular-400.otf` | SIL OFL 1.1 | no |
| `xkcd/xkcd.otf`, `xkcd/xkcd-script.ttf` | **CC BY-NC 3.0** | no |

The xkcd faces are Attribution-**NonCommercial**. That does not combine with this project's
GPL-3.0-or-later, which grants commercial use and redistribution, so they are carried here for
reference and are deliberately not compiled into the viewer. Anything that starts using them makes the
binary undistributable under the GPL.

Font Awesome is embedded as a subset of the icons the viewer actually draws: the solid face is about
1 MB whole and 9 kB cut to the glyphs in use.
