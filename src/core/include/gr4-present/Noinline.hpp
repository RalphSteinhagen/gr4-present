#ifndef GR4_PRESENT_NOINLINE_HPP
#define GR4_PRESENT_NOINLINE_HPP

// keeps a cold path (error, diagnostic, first-use set-up) out of the hot code that calls it, so only the hot loop is
// inlined; GCC's `noipa` also stops it from being cloned or analysed into its callers (after GR4's `NO_INLINE`,
// gnuradio-4.0/Settings.hpp)
#if defined(__clang__)
#define GR4_PRESENT_NOINLINE [[gnu::noinline]]
#elif defined(__GNUC__)
#define GR4_PRESENT_NOINLINE [[gnu::noinline, gnu::noipa]]
#else
#define GR4_PRESENT_NOINLINE
#endif

#endif // GR4_PRESENT_NOINLINE_HPP
