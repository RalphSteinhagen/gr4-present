# Bake a binary file into a header as a byte array. The splash textures must be available before any resource loading
# works, so they cannot be read from disk or fetched.
function(gr4_present_embed_binaries output_header)
  # the guard is derived from the file being written: a fixed one makes the second such header included in a translation
  # unit vanish, and takes the symbols it carries with it
  get_filename_component(_name "${output_header}" NAME_WE)
  string(TOUPPER "GR4_PRESENT_${_name}_HPP" _guard)
  set(_content
      "#ifndef ${_guard}\n#define ${_guard}\n\n#include <array>\n#include <cstddef>\n\nnamespace gr::present {\n")
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
  string(APPEND _content "\n} // namespace gr::present\n\n#endif // ${_guard}\n")
  file(
    GENERATE
    OUTPUT "${output_header}"
    CONTENT "${_content}")
endfunction()
