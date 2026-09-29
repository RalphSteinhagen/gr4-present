# Fira Math

`FiraMath-Regular.clm2` holds the OpenType MATH metrics and the glyph outlines of Fira Math, in the format
MicroTeX reads. It is copied from MicroTeX's `res/firamath`, which generates it from the font; no generator is
carried here and none is run during a build.

Only the `.clm2` is vendored. The `.otf` itself is not needed: the viewer compiles MicroTeX with
`GLYPH_RENDER_TYPE=1`, so every glyph is drawn from the outlines in this file and no typeface is ever loaded.

Fira Math is the smallest of the four families MicroTeX ships, 520 kB against 4.4 MB for XITS, and being an
OpenType MATH font it carries a complete mathematical alphabet.

Licensed under the SIL Open Font License 1.1; see `LICENSE.txt`.
