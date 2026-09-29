#include <gr4-present/EffectSource.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <ranges>
#include <utility>

namespace gr::present {

namespace {

constexpr std::array<std::string_view, 8> kUseNames{"transition", "background", "viewport", "overlay", "reveal", "camera", "pointer", "break"};
constexpr std::array<std::string_view, 7> kPassNames{"Common", "BufferA", "BufferB", "BufferC", "BufferD", "CubeA", "Image"};
constexpr std::array<std::string_view, 7> kParameterTypeNames{"float", "int", "bool", "vec2", "vec3", "vec4", "colour"};

[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1UZ);
}

[[nodiscard]] std::vector<std::string_view> wordsOf(std::string_view text) {
    std::vector<std::string_view> words;
    for (const auto word : text | std::views::split(' ')) {
        const std::string_view piece = trimmed(std::string_view{word.begin(), word.end()});
        if (!piece.empty()) {
            words.push_back(piece);
        }
    }
    return words;
}

[[nodiscard]] std::optional<float> floatOf(std::string_view text) noexcept {
    text = trimmed(text);
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1UZ);
    }
    float value             = 0.0f;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool isIdentifier(std::string_view text) noexcept {
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    const auto digit  = [](char c) { return c >= '0' && c <= '9'; };
    return !text.empty() && letter(text.front()) && std::ranges::all_of(text, [&](char c) { return letter(c) || digit(c); });
}

[[nodiscard]] std::optional<std::array<float, 4>> colourOf(std::string_view text) noexcept {
    if (text.size() != 7UZ && text.size() != 9UZ) {
        return std::nullopt;
    }
    if (text.front() != '#') {
        return std::nullopt;
    }
    std::array<float, 4> colour{0.0f, 0.0f, 0.0f, 1.0f};
    for (std::size_t component = 0UZ; component < (text.size() - 1UZ) / 2UZ; ++component) {
        unsigned   byte         = 0U;
        const auto digits       = text.substr(1UZ + 2UZ * component, 2UZ);
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), byte, 16);
        if (error != std::errc{} || end != digits.data() + digits.size()) {
            return std::nullopt;
        }
        colour[component] = static_cast<float>(byte) / 255.0f;
    }
    return colour;
}

} // namespace

std::string cubemapFacePath(std::string_view asset, std::size_t face) {
    if (face == 0UZ) {
        return std::string{asset};
    }
    const auto             dot       = asset.rfind('.');
    const auto             slash     = asset.rfind('/');
    const bool             extension = dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
    const std::string_view stem      = extension ? asset.substr(0UZ, dot) : asset;
    const std::string_view tail      = extension ? asset.substr(dot) : std::string_view{};
    return std::format("{}_{}{}", stem, face, tail);
}

namespace {

/// `@channelN <BufferA|…|asset> [type=…] [filter=…] [wrap=…] [vflip|vflip=false]`
[[nodiscard]] std::expected<ChannelBinding, std::string> bindingOf(std::string_view value) {
    const std::vector<std::string_view> words = wordsOf(value);
    if (words.empty()) {
        return std::unexpected{std::string{"names no buffer or asset"}};
    }
    std::optional<ChannelSource> type;
    std::optional<ChannelFilter> filter;
    std::optional<ChannelWrap>   wrap;
    std::optional<bool>          vflip;
    for (const std::string_view option : words | std::views::drop(1)) {
        const auto             equals = option.find('=');
        const std::string_view key    = option.substr(0UZ, equals);
        const std::string_view text   = equals == std::string_view::npos ? std::string_view{} : option.substr(equals + 1UZ);
        if (key == "type" && text == "image") {
            type = ChannelSource::image;
        } else if (key == "type" && text == "cubemap") {
            type = ChannelSource::cubemap;
        } else if (key == "type" && text == "volume") {
            type = ChannelSource::volume;
        } else if (key == "type") {
            return std::unexpected{std::format("has an unknown type '{}'", text)};
        } else if (key == "filter" && text == "nearest") {
            filter = ChannelFilter::nearest;
        } else if (key == "filter" && text == "linear") {
            filter = ChannelFilter::linear;
        } else if (key == "filter" && text == "mipmap") {
            filter = ChannelFilter::mipmap;
        } else if (key == "filter") {
            return std::unexpected{std::format("has an unknown filter '{}'", text)};
        } else if (key == "wrap" && text == "clamp") {
            wrap = ChannelWrap::clamp;
        } else if (key == "wrap" && text == "repeat") {
            wrap = ChannelWrap::repeat;
        } else if (key == "wrap") {
            return std::unexpected{std::format("has an unknown wrap '{}'", text)};
        } else if (key == "vflip" && (text.empty() || text == "true")) {
            vflip = true;
        } else if (key == "vflip" && text == "false") {
            vflip = false;
        } else if (key == "vflip") {
            return std::unexpected{std::format("has an unknown vflip '{}'", text)};
        } else {
            return std::unexpected{std::format("has an unknown option '{}'", option)};
        }
    }
    // each kind of input has its own defaults; options the author wrote override them, in any order
    ChannelBinding binding;
    if (const auto pass = parsePassKind(words.front()); pass.has_value() && *pass != PassKind::common && *pass != PassKind::image) {
        if (type.has_value()) {
            return std::unexpected{std::format("gives a type to {}, whose type is its own", words.front())};
        }
        binding = ChannelBinding{.source = *pass == PassKind::cubeA ? ChannelSource::cubemap : ChannelSource::pass, .asset = {}, .pass = *pass, .filter = ChannelFilter::linear, .wrap = ChannelWrap::clamp, .vflip = false};
    } else {
        const ChannelSource source = type.value_or(words.front().ends_with(".bin") ? ChannelSource::volume : ChannelSource::image);
        binding                    = source == ChannelSource::image ? ChannelBinding{.source = source, .asset = std::string{words.front()}, .pass = PassKind::image, .filter = ChannelFilter::mipmap, .wrap = ChannelWrap::repeat, .vflip = true} : source == ChannelSource::cubemap ? ChannelBinding{.source = source, .asset = std::string{words.front()}, .pass = PassKind::image, .filter = ChannelFilter::mipmap, .wrap = ChannelWrap::clamp, .vflip = false} : ChannelBinding{.source = source, .asset = std::string{words.front()}, .pass = PassKind::image, .filter = ChannelFilter::linear, .wrap = ChannelWrap::repeat, .vflip = false};
    }
    binding.filter = filter.value_or(binding.filter);
    binding.wrap   = wrap.value_or(binding.wrap);
    binding.vflip  = vflip.value_or(binding.vflip);
    return binding;
}

/// `@param <name> <type> <default> [min max]`
[[nodiscard]] std::expected<EffectParameter, std::string> parameterOf(std::string_view value) {
    const std::vector<std::string_view> words = wordsOf(value);
    if (words.size() != 3UZ && words.size() != 5UZ) {
        return std::unexpected{std::string{"wants a name, a type, a default and optionally a minimum and a maximum"}};
    }
    if (!isIdentifier(words[0])) {
        return std::unexpected{std::format("'{}' is not a GLSL name", words[0])};
    }
    const auto type = parseParameterType(words[1]);
    if (!type.has_value()) {
        return std::unexpected{std::format("has an unknown type '{}'", words[1])};
    }
    const auto fallback = parseParameterValue(*type, words[2]);
    if (!fallback.has_value()) {
        return std::unexpected{std::format("'{}' is not a {}", words[2], words[1])};
    }
    EffectParameter parameter{.name = std::string{words[0]}, .type = *type, .value = *fallback, .range = std::nullopt};
    if (words.size() == 5UZ) {
        const auto low  = floatOf(words[3]);
        const auto high = floatOf(words[4]);
        if (!low.has_value() || !high.has_value() || *low > *high) {
            return std::unexpected{std::format("has no valid range '{} {}'", words[3], words[4])};
        }
        parameter.range = std::array{*low, *high};
    }
    return parameter;
}

struct Line {
    std::string_view text;
    std::size_t      number = 1UZ;
};

[[nodiscard]] std::vector<Line> linesOf(std::string_view text) {
    std::vector<Line> lines;
    std::size_t       number = 1UZ;
    for (const auto piece : text | std::views::split('\n')) {
        std::string_view line{piece.begin(), piece.end()};
        if (line.ends_with('\r')) {
            line.remove_suffix(1UZ);
        }
        lines.push_back(Line{.text = line, .number = number++});
    }
    if (!lines.empty() && lines.back().text.empty() && text.ends_with('\n')) {
        lines.pop_back(); // the newline closing the last line opens no line of its own
    }
    return lines;
}

/// `//--- pass: Name` → the pass, or empty when the line is not a marker
[[nodiscard]] std::optional<std::string_view> markerOf(std::string_view line) noexcept {
    line = trimmed(line);
    if (!line.starts_with("//---")) {
        return std::nullopt;
    }
    line = trimmed(line.substr(5UZ));
    if (!line.starts_with("pass:")) {
        return std::nullopt;
    }
    return trimmed(line.substr(5UZ));
}

/// `// @key value` → key and value
[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> headerOf(std::string_view line) noexcept {
    line = trimmed(line);
    if (!line.starts_with("//") || line.starts_with("//---")) {
        return std::nullopt;
    }
    line = trimmed(line.substr(2UZ));
    if (!line.starts_with('@')) {
        return std::nullopt;
    }
    const auto space = line.find_first_of(" \t");
    return std::pair{line.substr(1UZ, space == std::string_view::npos ? std::string_view::npos : space - 1UZ), space == std::string_view::npos ? std::string_view{} : trimmed(line.substr(space))};
}

[[nodiscard]] std::optional<std::size_t> channelIndexOf(std::string_view key) noexcept {
    if (key.size() == 8UZ && key.starts_with("channel") && key[7] >= '0' && key[7] <= '3') {
        return static_cast<std::size_t>(key[7] - '0');
    }
    return std::nullopt;
}

} // namespace

bool EffectSource::supports(EffectUse use) const noexcept { return uses.empty() || std::ranges::contains(uses, use); }

const EffectPass* EffectSource::pass(PassKind kind) const noexcept {
    const auto found = std::ranges::find(passes, kind, &EffectPass::kind);
    return found == passes.end() ? nullptr : &*found;
}

std::optional<ChannelBinding> EffectSource::binding(const EffectPass& effectPass, std::size_t channel) const {
    if (channel >= 4UZ) {
        return std::nullopt;
    }
    return effectPass.channels[channel].has_value() ? effectPass.channels[channel] : channels[channel];
}

const EffectParameter* EffectSource::parameter(std::string_view parameterName) const noexcept {
    const auto found = std::ranges::find(parameters, parameterName, &EffectParameter::name);
    return found == parameters.end() ? nullptr : &*found;
}

std::string_view name(EffectUse use) noexcept { return kUseNames[static_cast<std::size_t>(use)]; }

std::optional<EffectUse> parseEffectUse(std::string_view text) noexcept {
    const auto found = std::ranges::find(kUseNames, text);
    return found == kUseNames.end() ? std::nullopt : std::optional{static_cast<EffectUse>(found - kUseNames.begin())};
}

std::string_view name(PassKind kind) noexcept { return kPassNames[static_cast<std::size_t>(kind)]; }

std::optional<PassKind> parsePassKind(std::string_view text) noexcept {
    const auto found = std::ranges::find(kPassNames, text);
    return found == kPassNames.end() ? std::nullopt : std::optional{static_cast<PassKind>(found - kPassNames.begin())};
}

std::optional<ParameterType> parseParameterType(std::string_view text) noexcept {
    const auto found = std::ranges::find(kParameterTypeNames, text);
    return found == kParameterTypeNames.end() ? std::nullopt : std::optional{static_cast<ParameterType>(found - kParameterTypeNames.begin())};
}

std::optional<std::array<float, 4>> parseParameterValue(ParameterType type, std::string_view text) {
    text = trimmed(text);
    switch (type) {
    case ParameterType::colour: return colourOf(text);
    case ParameterType::boolean:
        if (text == "true" || text == "1") {
            return std::array{1.0f, 0.0f, 0.0f, 0.0f};
        }
        if (text == "false" || text == "0") {
            return std::array{0.0f, 0.0f, 0.0f, 0.0f};
        }
        return std::nullopt;
    case ParameterType::integer: {
        int value               = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
            return std::nullopt;
        }
        return std::array{static_cast<float>(value), 0.0f, 0.0f, 0.0f};
    }
    default: break;
    }
    const std::size_t    components = type == ParameterType::vec2 ? 2UZ : type == ParameterType::vec3 ? 3UZ : type == ParameterType::vec4 ? 4UZ : 1UZ;
    std::array<float, 4> value{};
    std::size_t          count = 0UZ;
    for (const auto piece : text | std::views::split(',')) {
        if (count == components) {
            return std::nullopt;
        }
        const auto component = floatOf(std::string_view{piece.begin(), piece.end()});
        if (!component.has_value()) {
            return std::nullopt;
        }
        value[count++] = *component;
    }
    if (count != components) {
        return std::nullopt;
    }
    return value;
}

std::expected<ParsedEffect, std::string> parseEffect(std::string_view effectName, std::string_view text) {
    ParsedEffect  parsed;
    EffectSource& effect = parsed.effect;
    effect.name          = std::string{effectName};
    const auto warn      = [&](std::size_t line, std::string_view what) { parsed.warnings.push_back(std::format("{}:{}: {}", effectName, line, what)); };

    const std::vector<Line>    lines      = linesOf(text);
    const bool                 marked     = std::ranges::any_of(lines, [](const Line& line) { return markerOf(line.text).has_value(); });
    std::optional<std::size_t> open       = marked ? std::nullopt : std::optional{0UZ}; // index into effect.passes being filled
    bool                       inHeader   = true;                                       // the file's leading comment block
    bool                       pastMarker = false;                                      // code after a marker belongs to a pass, or to one that was ignored
    std::string                code;

    if (!marked) {
        effect.passes.push_back(EffectPass{.kind = PassKind::image, .code = std::string{text}, .firstLine = 1UZ, .channels = {}});
    }
    const auto closeSection = [&] {
        if (marked && open.has_value()) {
            effect.passes[*open].code = std::move(code);
            code.clear();
        }
    };

    for (const Line& line : lines) {
        if (const auto marker = markerOf(line.text); marker.has_value()) {
            closeSection();
            inHeader        = false;
            pastMarker      = true;
            const auto kind = parsePassKind(*marker);
            if (!kind.has_value()) {
                warn(line.number, std::format("'{}' is not a pass; its section is ignored", *marker));
                open.reset();
                continue;
            }
            if (effect.pass(*kind) != nullptr) {
                warn(line.number, std::format("a second {} pass is ignored", *marker));
                open.reset();
                continue;
            }
            effect.passes.push_back(EffectPass{.kind = *kind, .code = {}, .firstLine = line.number + 1UZ, .channels = {}});
            open = effect.passes.size() - 1UZ;
            continue;
        }
        if (marked && open.has_value()) {
            code.append(line.text);
            code.push_back('\n');
        } else if (marked && !pastMarker && !trimmed(line.text).empty() && !trimmed(line.text).starts_with("//")) {
            warn(line.number, "code before the first pass marker belongs to no pass and is ignored");
        }
        const std::string_view plain = trimmed(line.text);
        if (!plain.empty() && !plain.starts_with("//")) {
            inHeader = false;
        }
        const auto header = headerOf(line.text);
        if (!header.has_value()) {
            continue;
        }
        const auto [key, value] = *header;
        if (const auto channel = channelIndexOf(key); channel.has_value()) {
            auto binding = bindingOf(value);
            if (!binding.has_value()) {
                warn(line.number, std::format("@{} {}; ignored", key, binding.error()));
            } else if (marked && open.has_value()) {
                effect.passes[*open].channels[*channel] = std::move(*binding);
            } else if (!marked || inHeader) {
                effect.channels[*channel] = std::move(*binding);
            }
            continue;
        }
        if (!inHeader) {
            continue; // an `@` in a comment further down is the author's prose
        }
        if (key == "name") {
            effect.name = std::string{value};
        } else if (key == "author") {
            effect.author = std::string{value};
        } else if (key == "source") {
            effect.source = std::string{value};
        } else if (key == "licence") {
            effect.licence = std::string{value};
        } else if (key == "uses") {
            std::string list{value};
            std::ranges::replace(list, ',', ' ');
            for (const std::string_view word : wordsOf(list)) {
                if (const auto use = parseEffectUse(word); use.has_value()) {
                    effect.uses.push_back(*use);
                } else {
                    warn(line.number, std::format("'{}' is not a use; ignored", word));
                }
            }
        } else if (key == "param") {
            auto parameter = parameterOf(value);
            if (!parameter.has_value()) {
                warn(line.number, std::format("@param {}; ignored", parameter.error()));
            } else if (effect.parameter(parameter->name) != nullptr) {
                warn(line.number, std::format("@param {} is declared twice; the second is ignored", parameter->name));
            } else {
                const bool single = parameter->type == ParameterType::scalar || parameter->type == ParameterType::integer;
                if (parameter->range.has_value() && single && (parameter->value[0] < (*parameter->range)[0] || parameter->value[0] > (*parameter->range)[1])) {
                    warn(line.number, std::format("@param {}: its default lies outside {} … {}; clamped", parameter->name, (*parameter->range)[0], (*parameter->range)[1]));
                    parameter->value[0] = std::clamp(parameter->value[0], (*parameter->range)[0], (*parameter->range)[1]);
                }
                effect.parameters.push_back(std::move(*parameter));
            }
        } else if (key == "still" || key == "duration") {
            const auto seconds = floatOf(value);
            if (!seconds.has_value() || *seconds < 0.0f) {
                warn(line.number, std::format("@{} wants seconds, not '{}'; ignored", key, value));
            } else {
                (key == "still" ? effect.still : effect.duration) = *seconds;
            }
        } else {
            warn(line.number, std::format("@{} is not a header key; ignored", key));
        }
    }
    closeSection();

    std::ranges::stable_sort(effect.passes, std::less<>{}, &EffectPass::kind);
    const EffectPass* image = effect.pass(PassKind::image);
    if (image == nullptr) {
        return std::unexpected{std::format("{}: has no Image pass", effectName)};
    }
    if (trimmed(image->code).empty()) {
        return std::unexpected{std::format("{}: its Image pass is empty", effectName)};
    }
    for (const EffectPass& effectPass : effect.passes) {
        for (std::size_t channel = 0UZ; channel < 4UZ; ++channel) {
            const auto bound = effect.binding(effectPass, channel);
            if (bound.has_value() && bound->source != ChannelSource::image && bound->source != ChannelSource::volume && bound->asset.empty() && effect.pass(bound->pass) == nullptr) {
                warn(effectPass.firstLine, std::format("{} reads {} on channel {}, which the effect does not define", name(effectPass.kind), name(bound->pass), channel));
            }
        }
    }
    return parsed;
}

bool isBuiltInMotion(std::string_view word) noexcept {
    constexpr std::array<std::string_view, 12> kMotions{"none", "cut", "fade", "fade-through", "camera", "zoom", "morph", "push", "cover", "uncover", "rise", "wipe"};
    constexpr std::array<std::string_view, 4>  kDirections{"-left", "-right", "-up", "-down"};
    if (std::ranges::contains(kMotions, word) || word == "grow") {
        return true;
    }
    for (const std::string_view slide : {std::string_view{"push"}, std::string_view{"cover"}, std::string_view{"uncover"}}) {
        if (word.starts_with(slide) && std::ranges::contains(kDirections, word.substr(slide.size()))) {
            return true;
        }
    }
    return false;
}

std::string_view effectNamed(std::string_view value) noexcept {
    const std::vector<std::string_view> words = wordsOf(value);
    if (words.empty()) {
        return {};
    }
    if (words.front() == "camera" || words.front() == "zoom") {
        return words.size() > 1UZ && !words[1].contains('=') ? words[1] : std::string_view{};
    }
    return isBuiltInMotion(words.front()) || words.front().contains('=') ? std::string_view{} : words.front();
}

std::string effectPath(std::string_view effectName) { return effectName.contains('/') ? std::string{effectName} : std::format("effects/{}.glsl", effectName); }

std::vector<std::string> effectAssets(const EffectSource& effect) {
    std::vector<std::string> assets;
    const auto               add = [&](const std::optional<ChannelBinding>& binding) {
        if (!binding.has_value() || binding->asset.empty()) {
            return;
        }
        const std::size_t faces = binding->source == ChannelSource::cubemap ? 6UZ : 1UZ;
        for (std::size_t face = 0UZ; face < faces; ++face) {
            std::string path = cubemapFacePath(binding->asset, face);
            if (!std::ranges::contains(assets, path)) {
                assets.push_back(std::move(path));
            }
        }
    };
    std::ranges::for_each(effect.channels, add);
    for (const EffectPass& effectPass : effect.passes) {
        std::ranges::for_each(effectPass.channels, add);
    }
    return assets;
}

std::vector<std::string> effectAssetsOf(std::string_view text) {
    const auto parsed = parseEffect("", text);
    return parsed.has_value() ? effectAssets(parsed->effect) : std::vector<std::string>{};
}

} // namespace gr::present
