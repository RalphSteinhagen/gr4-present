# Media credits

Every file here is public domain, made for this repository, or the author's own work, and none carries a
share-alike obligation.

`Science_FAIR.webp` is the exception: its **CC BY-NC 4.0** `NonCommercial` term cannot be combined with
`GPL-3.0-or-later`, so it is loaded at run time and never compiled in, as `assets/xkcd/` is. Remove it
to reuse this deck commercially.

| file | source | author | licence |
| --- | --- | --- | --- |
| `hubble-deep-field.jpg` | [Hubble Ultra Deep Field](https://commons.wikimedia.org/wiki/File:Hubble_ultra_deep_field.jpg) | NASA and the European Space Agency | public domain |
| `hubble-deep-field.webp` | the file above, re-encoded here with ffmpeg | as above | public domain |
| `horse-in-motion.gif` | [The Horse in Motion](https://commons.wikimedia.org/wiki/File:The_Horse_in_Motion-anim.gif), Eadweard Muybridge, 1878; animation by Nevit Dilmen, from the Library of Congress print | Eadweard Muybridge | public domain |
| `horse-in-motion.webp` | the file above, re-encoded here with ffmpeg | as above | public domain |
| `alpha-circles.png` | drawn for this repository | gr4-present | CC BY 4.0, with the rest of this deck |
| `Solvay_conference_1927.webp` | [Solvay conference 1927](https://commons.wikimedia.org/wiki/File:Solvay_conference_1927.jpg), Institut International de Physique Solvay, Brussels, 29 October 1927 | Benjamin Couprie | public domain (published in Belgium more than 70 years ago) |
| `Science_FAIR.webp` | the author's own work, encoded here from a 3840x2160 PNG master kept outside the repository | Ralph J. Steinhagen | **CC BY-NC 4.0** |

The two Hubble files were scaled to 1200 px and the horse reduced to 96 colours, so the repository
does not carry several megabytes to show that a JPEG decodes.

`Steamboat_Willie_(1928)_by_Walt_Disney_extract.webm` is an extract from *Steamboat Willie*, Walt
Disney and Ub Iwerks, 1928, which entered the United States public domain on 1 January 2024 when its
95-year copyright term expired. VP8 video and Vorbis sound in a WebM container, which is what the
viewer decodes and what browsers play natively. It doubles as the fixture the video decoder is tested
against.

`Science_FAIR.webp` keeps its full 3840x2160 because the raster camera magnifies parts of it about
threefold. WebP quality 95 took the PNG master from 15,248,995 bytes to 3,094,678 and is
indistinguishable at that magnification; 85 and 90 visibly smooth the pencil hatching on the tent.

## Figures

`../figures/fair-logo.svg`, a copy of `assets/FAIR_Logo.svg`, is the logo of FAIR -- Facility for Antiprotons and
Ion Research in Europe GmbH -- and its trademark, shown with FAIR's permission. It is not covered by the deck's
licence.

## Workflows

`functions.grc` and `spectrum.grc` in `workflows/` are sub-graphs of OpenDigitizer's own demo
dashboard, `src/ui/assets/sampleDashboards/DemoDashboard.grc`, copied block for block and split where
the graph was already disjoint. Nothing in them was rewritten but two things: the `dashboard:` section,
which describes a window layout this viewer does not use, was dropped, and `functions.grc` lost the fixed
colours of its two sinks, so its charts take the deck's palette in light and dark alike.

| file | blocks | connections |
| --- | --- | --- |
| `functions.grc` | 5 | 4 |
| `spectrum.grc` | 3 | 2 |

`controlled-sines.grc` is OpenDigitizer's `src/ui/assets/sampleDashboards/UIControlledSines.grc`,
copied unchanged; its four `ImControlNumber` blocks are a toolbar's frequency and amplitude controls.
`modulated-sines.grc` turns it into an amplitude modulation for the slide that shows its toolbar and
flow graph: the carrier (sine 1) times a modulation (sine 2), with GNU Radio's `Multiply`, plus a Gaussian
noise source with its own control, added with GNU Radio's `Add`; the time chart shows the carrier and the
modulated signal, and the magnitude of its FFT is charted beside it, as `DemoDashboard.grc` charts its own.
The viewer never draws the scheduler's play, pause and stop, whatever a file's `scheduler_ui` says.

Source: [OpenDigitizer](https://github.com/fair-acc/opendigitizer), FAIR — Facility for Antiproton and
Ion Research, under that project's own licence. They are here so that a live region draws something a
reader can compare against what OpenDigitizer draws from the same file.

The deck's own face lives beside the media, in `../fonts/`, with its licence text:

| file | source | author | licence |
| --- | --- | --- | --- |
| `../fonts/PatrickHand-Regular.ttf` | [google/fonts, ofl/patrickhand](https://github.com/google/fonts/tree/main/ofl/patrickhand), unmodified | Patrick Wagesreiter | SIL OFL 1.1 (`../fonts/PatrickHand-OFL.txt`) |
