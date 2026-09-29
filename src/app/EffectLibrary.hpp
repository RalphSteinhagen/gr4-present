#ifndef GR4_PRESENT_EFFECT_LIBRARY_HPP
#define GR4_PRESENT_EFFECT_LIBRARY_HPP

#include "EffectRenderer.hpp"

#include <gr4-present/Diagnostics.hpp>
#include <gr4-present/EffectSource.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gr::present {

/**
 * The effects a deck can name: its own `effects/` files first, then those built into the viewer.
 *
 * Each effect is parsed once, on first use; its warnings and errors go to the deck's diagnostics, so an author sees a
 * typo in a header or a shader that does not compile in the same list as a missing figure.
 */
class EffectLibrary {
public:
    using PackageBytes = std::function<std::span<const std::uint8_t>(std::string_view packagePath)>;

    bool enabled = true; // false where the GL context cannot run effect shaders; nothing is drawn through one then

    /// forgets every effect; `deck` reads a file of the presentation the loader fetched
    void reset(PackageBytes deck, Diagnostics* diagnostics);

    /// the effect a name or path stands for, or nothing when neither the deck nor the viewer has it
    [[nodiscard]] const EffectSource* find(std::string_view effectName);

    /// a renderer of its own for one use of an effect, or nothing when the effect is unknown
    [[nodiscard]] std::unique_ptr<EffectRenderer> renderer(std::string_view effectName);

    /// reports a renderer's compile or link failure once per effect
    void reportFailure(const EffectRenderer& renderer);
    /// reports what a renderer found wrong with its channel assets
    void reportProblems(EffectRenderer& renderer);

    /// the parameters a `k=v …` list sets, checked against what the effect declares; what does not fit is reported
    [[nodiscard]] std::map<std::string, std::array<float, 4>, std::less<>> parametersOf(const EffectSource& effect, std::string_view settings);

    /// the names of the effects built into the viewer
    [[nodiscard]] static std::span<const std::string_view> bundledNames() noexcept;

private:
    struct Entry {
        std::optional<EffectSource> effect;
        bool                        fromDeck = false;
        bool                        reported = false;
    };

    PackageBytes                              _deck;
    Diagnostics*                              _diagnostics = nullptr;
    std::map<std::string, Entry, std::less<>> _entries;

    void report(std::string subject, std::string detail);
};

} // namespace gr::present

#endif // GR4_PRESENT_EFFECT_LIBRARY_HPP
