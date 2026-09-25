# Bake a binary file into a header as a byte array. The splash textures must be available before any resource loading
# works, so they cannot be read from disk or fetched.
function(gr4_present_embed_binaries output_header)
  set(_content
      "#ifndef GR4_PRESENT_EMBEDDED_LOGOS_HPP\n#define GR4_PRESENT_EMBEDDED_LOGOS_HPP\n\n#include <array>\n#include <cstddef>\n\nnamespace gr::present {\n"
  )
  foreach(_pair IN LISTS ARGN)
    string(
      REPLACE "="
              ";"
              _parts
              "${_pair}")
    list(
      GET
      _parts
      0
      _symbol)
    list(
      GET
      _parts
      1
      _path)
    file(
      READ
      "${_path}"
      _hex
      HEX)
    string(
      REGEX
      REPLACE "([0-9a-f][0-9a-f])"
              "0x\\1,"
              _bytes
              "${_hex}")
    string(LENGTH "${_hex}" _hex_length)
    math(EXPR _size "${_hex_length} / 2")
    string(APPEND _content "\ninline constexpr std::array<unsigned char, ${_size}> ${_symbol}{${_bytes}};\n")
  endforeach()
  string(APPEND _content "\n} // namespace gr::present\n\n#endif // GR4_PRESENT_EMBEDDED_LOGOS_HPP\n")
  file(
    GENERATE
    OUTPUT "${output_header}"
    CONTENT "${_content}")
endfunction()
