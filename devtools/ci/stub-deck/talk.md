# gr4-present @VERSION@<br>the @FLAVOUR@ viewer, without a deck of its own {#viewer}

This folder is the viewer only (@BUILD@). The full viewer, with live GNU Radio 4 graphs, is online at [@SITE@](@SITE@).

To present a deck with this copy:

- **here, offline:** put the deck's folder or `.tar.gz` beside `index.html` and run `sh serve.sh` (or `serve.cmd`), then open `?presentation=<name>`; or, with no server, `sh launch-chromium.sh <name>`
- **as this deck:** replace `default/` with the deck, and the viewer opens it without any address parameter
- **hosted elsewhere:** open `index.html?presentation=<address of the deck's folder>`; its host must allow other origins to read it, as GitHub Pages does

README.txt beside `index.html` says more.
