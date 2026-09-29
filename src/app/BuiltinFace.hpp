#ifndef GR4_PRESENT_BUILTIN_FACE_HPP
#define GR4_PRESENT_BUILTIN_FACE_HPP

#include <cstdint>
#include <span>

namespace gr::present {

enum class BuiltinFace : std::uint8_t { body, bold, italic, boldItalic, mono };

/// the TrueType bytes of a Liberation 2.1.5 face, inflated on first use and kept for the program's life
[[nodiscard]] std::span<const std::uint8_t> builtinFaceTtf(BuiltinFace face);

} // namespace gr::present

#endif // GR4_PRESENT_BUILTIN_FACE_HPP
