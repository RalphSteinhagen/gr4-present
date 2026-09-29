# QR Code generator

Nayuki's QR Code generator, the C++ implementation, vendored rather than fetched: it is two files with no
dependencies, and a build-time fetch for that is more machinery than the code it brings in.

Copied from <https://github.com/nayuki/QR-Code-generator> at `3c6d0b3cefb4e049dc337e82237c9644399716a8`
(2026-08-31), `cpp/qrcodegen.cpp` and `cpp/qrcodegen.hpp`, unmodified. MIT licensed; the notice is at the head of
each file.

It throws `std::length_error` when the text will not fit the largest QR version, which the caller catches — this
project's own code still must not throw.
