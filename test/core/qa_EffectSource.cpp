#include <boost/ut.hpp>

#include <gr4-present/EffectSource.hpp>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

namespace {

constexpr std::string_view kImageCode = "void mainImage(out vec4 colour, in vec2 fragCoord) { colour = vec4(1.0); }";

[[nodiscard]] std::string joined(std::initializer_list<std::string_view> lines) {
    std::string text;
    for (const std::string_view line : lines) {
        text += line;
        text += '\n';
    }
    return text;
}

[[nodiscard]] std::string withImage(std::initializer_list<std::string_view> headerLines) {
    std::string text = joined(headerLines);
    text += kImageCode;
    text += '\n';
    return text;
}

[[nodiscard]] std::string crlfOf(std::string_view text) {
    std::string converted;
    for (const char c : text) {
        if (c == '\n') {
            converted += '\r';
        }
        converted += c;
    }
    return converted;
}

[[nodiscard]] ParsedEffect parsedOk(std::string_view text, std::string_view fileName = "fx") {
    auto result = parseEffect(fileName, text);
    expect(result.has_value()) << "the effect text should parse";
    return result.has_value() ? std::move(*result) : ParsedEffect{};
}

[[nodiscard]] bool hasWarning(const ParsedEffect& parsed, std::string_view fragment) {
    return std::ranges::any_of(parsed.warnings, [&](const std::string& warning) { return warning.contains(fragment); });
}

[[nodiscard]] std::optional<ChannelBinding> fileBindingOf(std::string_view channelArguments) {
    const ParsedEffect parsed = parsedOk(withImage({std::string{"// @channel0 "} + std::string{channelArguments}}));
    return parsed.effect.channels[0];
}

[[nodiscard]] ParsedEffect parsedWithParameter(std::string_view parameterArguments) { return parsedOk(withImage({std::string{"// @param "} + std::string{parameterArguments}})); }

constexpr float byteOf(int value) { return static_cast<float>(value) / 255.0f; }

} // namespace

const suite<"EffectSource header keys"> headerTests = [] {
    "every header key is read from the leading comment block"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @name   plasma", "// @author Ada Lovelace", "// @source https://example.org/plasma", "// @licence CC0-1.0", "// @uses   background viewport", "// @still  2.5", "// @duration 1.5"}), "file");
        expect(parsed.warnings.empty());
        expect(eq(parsed.effect.name, std::string{"plasma"}));
        expect(eq(parsed.effect.author, std::string{"Ada Lovelace"}));
        expect(eq(parsed.effect.source, std::string{"https://example.org/plasma"}));
        expect(eq(parsed.effect.licence, std::string{"CC0-1.0"}));
        expect(eq(parsed.effect.uses.size(), 2UZ));
        expect(parsed.effect.uses[0] == EffectUse::background);
        expect(parsed.effect.uses[1] == EffectUse::viewport);
        expect(parsed.effect.still.has_value() && *parsed.effect.still == 2.5f);
        expect(parsed.effect.duration.has_value() && *parsed.effect.duration == 1.5f);
    };

    "an effect without a name key is named after its file"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @author Ada"}), "tunnel");
        expect(eq(parsed.effect.name, std::string{"tunnel"}));
        expect(!parsed.effect.still.has_value());
        expect(!parsed.effect.duration.has_value());
        expect(parsed.effect.uses.empty());
    };

    "uses may be separated by spaces or commas"_test = [] {
        const ParsedEffect spaced = parsedOk(withImage({"// @uses transition overlay"}));
        expect(eq(spaced.effect.uses.size(), 2UZ));
        const ParsedEffect commaAndSpace = parsedOk(withImage({"// @uses transition, overlay, reveal"}));
        expect(eq(commaAndSpace.effect.uses.size(), 3UZ));
        expect(commaAndSpace.warnings.empty());
    };

    "uses separated by bare commas are read as separate uses"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @uses reveal,camera,pointer"}));
        expect(parsed.warnings.empty()) << "a comma-separated list is a valid list";
        expect(eq(parsed.effect.uses.size(), 3UZ));
    };

    "every use name is accepted and break selects the pause use"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @uses transition background viewport overlay reveal camera pointer break"}));
        expect(parsed.warnings.empty());
        expect(parsed.effect.uses == std::vector<EffectUse>{EffectUse::transition, EffectUse::background, EffectUse::viewport, EffectUse::overlay, EffectUse::reveal, EffectUse::camera, EffectUse::pointer, EffectUse::pause});
    };

    "an effect with no uses supports every use and one with uses supports only those"_test = [] {
        const EffectSource open = parsedOk(withImage({"// @name open"})).effect;
        for (const EffectUse use : {EffectUse::transition, EffectUse::background, EffectUse::viewport, EffectUse::overlay, EffectUse::reveal, EffectUse::camera, EffectUse::pointer, EffectUse::pause}) {
            expect(open.supports(use));
        }
        const EffectSource limited = parsedOk(withImage({"// @uses background break"})).effect;
        expect(limited.supports(EffectUse::background));
        expect(limited.supports(EffectUse::pause));
        expect(!limited.supports(EffectUse::transition));
        expect(!limited.supports(EffectUse::overlay));
    };

    "an unknown use is warned about with its line and the other uses survive"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @author Ada", "// @uses background sparkle"}));
        expect(eq(parsed.warnings.size(), 1UZ));
        expect(hasWarning(parsed, "fx:2"));
        expect(eq(parsed.effect.uses.size(), 1UZ));
        expect(parsed.effect.uses[0] == EffectUse::background);
    };

    "an unknown key is warned about with effect name and line and the effect still loads"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @author Ada", "// @colour red", "// @licence MIT"}));
        expect(eq(parsed.warnings.size(), 1UZ));
        expect(hasWarning(parsed, "fx:2"));
        expect(eq(parsed.effect.licence, std::string{"MIT"}));
        expect(parsed.effect.pass(PassKind::image) != nullptr);
    };

    "a channel number beyond three is an unknown key"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @channel4 wood.png"}));
        expect(hasWarning(parsed, "fx:1"));
        expect(std::ranges::none_of(parsed.effect.channels, [](const auto& binding) { return binding.has_value(); }));
    };

    "at-lines after the first code line are not header keys"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @name early", kImageCode, "// @name late", "// @author late", "// @unknown late"}));
        expect(parsed.warnings.empty());
        expect(eq(parsed.effect.name, std::string{"early"}));
        expect(parsed.effect.author.empty());
    };

    "a blank line inside the leading comment block does not end it"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @name first", "", "// @author second", kImageCode}));
        expect(eq(parsed.effect.author, std::string{"second"}));
    };

    "still and duration accept zero and reject negative or non-numeric seconds"_test = [] {
        const ParsedEffect zero = parsedOk(withImage({"// @still 0", "// @duration 0"}));
        expect(zero.warnings.empty());
        expect(zero.effect.still.has_value() && *zero.effect.still == 0.0f);
        expect(zero.effect.duration.has_value() && *zero.effect.duration == 0.0f);

        const ParsedEffect negative = parsedOk(withImage({"// @still -1", "// @duration -0.5"}));
        expect(eq(negative.warnings.size(), 2UZ));
        expect(hasWarning(negative, "fx:1"));
        expect(hasWarning(negative, "fx:2"));
        expect(!negative.effect.still.has_value());
        expect(!negative.effect.duration.has_value());

        const ParsedEffect text = parsedOk(withImage({"// @still soon", "// @duration"}));
        expect(eq(text.warnings.size(), 2UZ));
        expect(!text.effect.still.has_value());
        expect(!text.effect.duration.has_value());
    };
};

const suite<"EffectSource parameters"> parameterTests = [] {
    "every parameter type is read with its default"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @param speed float 1.5", "// @param count int 7", "// @param enabled bool true", "// @param offset vec2 0.1,0.2", "// @param axis vec3 1,0,-1", "// @param tint vec4 0.1,0.2,0.3,0.4", "// @param glow colour #ff8000"}));
        expect(parsed.warnings.empty());
        expect(eq(parsed.effect.parameters.size(), 7UZ));

        const auto expectParameter = [&](std::string_view parameterName, ParameterType type, std::array<float, 4> value) {
            const EffectParameter* found = parsed.effect.parameter(parameterName);
            expect(found != nullptr) << parameterName;
            if (found != nullptr) {
                expect(found->type == type) << parameterName;
                expect(found->value == value) << parameterName;
                expect(!found->range.has_value()) << parameterName;
            }
        };
        expectParameter("speed", ParameterType::scalar, {1.5f, 0.0f, 0.0f, 0.0f});
        expectParameter("count", ParameterType::integer, {7.0f, 0.0f, 0.0f, 0.0f});
        expectParameter("enabled", ParameterType::boolean, {1.0f, 0.0f, 0.0f, 0.0f});
        expectParameter("offset", ParameterType::vec2, {0.1f, 0.2f, 0.0f, 0.0f});
        expectParameter("axis", ParameterType::vec3, {1.0f, 0.0f, -1.0f, 0.0f});
        expectParameter("tint", ParameterType::vec4, {0.1f, 0.2f, 0.3f, 0.4f});
        expectParameter("glow", ParameterType::colour, {1.0f, byteOf(128), 0.0f, 1.0f});
    };

    "parameters keep their declaration order"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @param zeta float 1", "// @param alpha float 2", "// @param mid float 3"}));
        expect(eq(parsed.effect.parameters.size(), 3UZ));
        expect(eq(parsed.effect.parameters[0].name, std::string{"zeta"}));
        expect(eq(parsed.effect.parameters[1].name, std::string{"alpha"}));
        expect(eq(parsed.effect.parameters[2].name, std::string{"mid"}));
    };

    "a colour without alpha is opaque and one with alpha reads each byte divided by 255"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @param rgb colour #102030", "// @param rgba colour #10203040", "// @param white colour #ffffff", "// @param clear colour #00000000"}));
        expect(parsed.warnings.empty());
        expect(parsed.effect.parameter("rgb")->value == std::array<float, 4>{byteOf(0x10), byteOf(0x20), byteOf(0x30), 1.0f});
        expect(parsed.effect.parameter("rgba")->value == std::array<float, 4>{byteOf(0x10), byteOf(0x20), byteOf(0x30), byteOf(0x40)});
        expect(parsed.effect.parameter("white")->value == std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
        expect(parsed.effect.parameter("clear")->value == std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    };

    "colour digits are case-insensitive"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @param lower colour #abcdef", "// @param upper colour #ABCDEF"}));
        expect(eq(parsed.effect.parameters.size(), 2UZ));
        expect(parsed.effect.parameter("lower")->value == parsed.effect.parameter("upper")->value);
        expect(parsed.effect.parameter("lower")->value[0] == byteOf(0xab));
    };

    "a bool accepts true, false, 1 and 0"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @param a bool true", "// @param b bool false", "// @param c bool 1", "// @param d bool 0"}));
        expect(parsed.warnings.empty());
        expect(parsed.effect.parameter("a")->value[0] == 1.0f);
        expect(parsed.effect.parameter("b")->value[0] == 0.0f);
        expect(parsed.effect.parameter("c")->value[0] == 1.0f);
        expect(parsed.effect.parameter("d")->value[0] == 0.0f);
    };

    "negative and fractional floats and negative ints are read"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @param bias float -0.25", "// @param shift int -3", "// @param scale float 1e-3"}));
        expect(parsed.warnings.empty());
        expect(parsed.effect.parameter("bias")->value[0] == -0.25f);
        expect(parsed.effect.parameter("shift")->value[0] == -3.0f);
        expect(parsed.effect.parameter("scale")->value[0] == 1e-3f);
    };

    "a range applies inclusive minimum and maximum"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @param speed float 1.0 0.1 10.0", "// @param count int 2 -4 8", "// @param offset vec2 0.5,0.5 0 1"}));
        expect(parsed.warnings.empty());
        expect(parsed.effect.parameter("speed")->range == std::optional{std::array<float, 2>{0.1f, 10.0f}});
        expect(parsed.effect.parameter("count")->range == std::optional{std::array<float, 2>{-4.0f, 8.0f}});
        expect(parsed.effect.parameter("offset")->range == std::optional{std::array<float, 2>{0.0f, 1.0f}});
    };

    "malformed parameters are warned about with their line and ignored"_test = [] {
        const std::vector<std::string_view> malformed{
            "1speed float 1.0",         // name starts with a digit
            "my-speed float 1.0",       // name is no GLSL identifier
            "speed float2 1.0",         // unknown type
            "speed float abc",          // not a number
            "speed float",              // no default
            "speed",                    // no type
            "",                         // nothing
            "speed int 1.5",            // not an integer
            "speed int one",            // not an integer
            "speed bool maybe",         // not a bool
            "speed bool 2",             // not a bool
            "speed vec2 0.1",           // too few components
            "speed vec2 0.1,0.2,0.3",   // too many components
            "speed vec3 0.1,0.2",       // too few components
            "speed vec4 1,2,3",         // too few components
            "speed vec2 0.1,x",         // non-numeric component
            "speed vec2 0.1,",          // empty component
            "speed colour ff0000",      // no hash
            "speed colour #ff00",       // wrong length
            "speed colour #ff00000",    // wrong length
            "speed colour #gg0000",     // not hex
            "speed colour red",         // not hex
            "speed float 1.0 5.0",      // a lone range bound
            "speed float 1.0 5.0 1.0",  // minimum above maximum
            "speed float 1.0 low high", // non-numeric range
            "speed float 1.0 0 1 2",    // too many words
        };
        for (const std::string_view arguments : malformed) {
            const ParsedEffect parsed = parsedWithParameter(arguments);
            expect(eq(parsed.warnings.size(), 1UZ)) << arguments;
            expect(hasWarning(parsed, "fx:1")) << arguments;
            expect(parsed.effect.parameters.empty()) << arguments;
        }
    };

    "a malformed parameter does not disturb its neighbours"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @param good float 1", "// @param bad float oops", "// @param fine int 2"}));
        expect(eq(parsed.warnings.size(), 1UZ));
        expect(hasWarning(parsed, "fx:2"));
        expect(eq(parsed.effect.parameters.size(), 2UZ));
        expect(parsed.effect.parameter("good") != nullptr);
        expect(parsed.effect.parameter("fine") != nullptr);
        expect(parsed.effect.parameter("bad") == nullptr);
    };

    "a duplicate parameter name is warned about and the second declaration is ignored"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @param speed float 1.0", "// @param speed int 5"}));
        expect(eq(parsed.warnings.size(), 1UZ));
        expect(hasWarning(parsed, "fx:2"));
        expect(eq(parsed.effect.parameters.size(), 1UZ));
        expect(parsed.effect.parameters[0].type == ParameterType::scalar);
        expect(parsed.effect.parameters[0].value[0] == 1.0f);
    };

    "parameter lookup finds a declared name and nothing else"_test = [] {
        const EffectSource effect = parsedOk(withImage({"// @param speed float 1.0"})).effect;
        expect(effect.parameter("speed") != nullptr);
        expect(effect.parameter("Speed") == nullptr);
        expect(effect.parameter("") == nullptr);
    };

    "parameter values parse directly per type"_test = [] {
        expect(parseParameterValue(ParameterType::scalar, "0.5") == std::optional{std::array{0.5f, 0.0f, 0.0f, 0.0f}});
        expect(parseParameterValue(ParameterType::integer, "42") == std::optional{std::array{42.0f, 0.0f, 0.0f, 0.0f}});
        expect(parseParameterValue(ParameterType::boolean, "true") == std::optional{std::array{1.0f, 0.0f, 0.0f, 0.0f}});
        expect(parseParameterValue(ParameterType::vec2, "0.1,0.2") == std::optional{std::array{0.1f, 0.2f, 0.0f, 0.0f}});
        expect(parseParameterValue(ParameterType::vec3, "1,2,3") == std::optional{std::array{1.0f, 2.0f, 3.0f, 0.0f}});
        expect(parseParameterValue(ParameterType::vec4, "1,2,3,4") == std::optional{std::array{1.0f, 2.0f, 3.0f, 4.0f}});
        expect(parseParameterValue(ParameterType::colour, "#ff0000") == std::optional{std::array{1.0f, 0.0f, 0.0f, 1.0f}});
        expect(!parseParameterValue(ParameterType::scalar, "").has_value());
        expect(!parseParameterValue(ParameterType::scalar, "1.0x").has_value());
        expect(!parseParameterValue(ParameterType::integer, "").has_value());
        expect(!parseParameterValue(ParameterType::vec2, "1,2,3").has_value());
        expect(!parseParameterValue(ParameterType::colour, "").has_value());
    };
};

const suite<"EffectSource channel bindings"> channelTests = [] {
    "a buffer target reads that pass with linear filter, clamp wrap and no flip"_test = [] {
        for (const auto& [target, kind] : {std::pair{"BufferA", PassKind::bufferA}, std::pair{"BufferB", PassKind::bufferB}, std::pair{"BufferC", PassKind::bufferC}, std::pair{"BufferD", PassKind::bufferD}}) {
            const auto binding = fileBindingOf(target);
            expect(binding.has_value()) << target;
            if (binding.has_value()) {
                expect(binding->source == ChannelSource::pass) << target;
                expect(binding->pass == kind) << target;
                expect(binding->asset.empty()) << target;
                expect(binding->filter == ChannelFilter::linear) << target;
                expect(binding->wrap == ChannelWrap::clamp) << target;
                expect(!binding->vflip) << target;
            }
        }
    };

    "CubeA is a cubemap source from the CubeA pass with linear filter, clamp wrap and no flip"_test = [] {
        const auto binding = fileBindingOf("CubeA");
        expect(binding.has_value());
        expect(binding->source == ChannelSource::cubemap);
        expect(binding->pass == PassKind::cubeA);
        expect(binding->asset.empty());
        expect(binding->filter == ChannelFilter::linear);
        expect(binding->wrap == ChannelWrap::clamp);
        expect(!binding->vflip);
    };

    "an asset path is an image with mipmap filter, repeat wrap and a flip"_test = [] {
        const auto binding = fileBindingOf("textures/wood.png");
        expect(binding.has_value());
        expect(binding->source == ChannelSource::image);
        expect(eq(binding->asset, std::string{"textures/wood.png"}));
        expect(binding->filter == ChannelFilter::mipmap);
        expect(binding->wrap == ChannelWrap::repeat);
        expect(binding->vflip);
    };

    "a bin asset is a volume with linear filter, repeat wrap and no flip"_test = [] {
        const auto binding = fileBindingOf("noise/grey.bin");
        expect(binding.has_value());
        expect(binding->source == ChannelSource::volume);
        expect(eq(binding->asset, std::string{"noise/grey.bin"}));
        expect(binding->filter == ChannelFilter::linear);
        expect(binding->wrap == ChannelWrap::repeat);
        expect(!binding->vflip);
    };

    "type cubemap makes an asset a cubemap with mipmap filter, clamp wrap and no flip"_test = [] {
        const auto binding = fileBindingOf("sky.png type=cubemap");
        expect(binding.has_value());
        expect(binding->source == ChannelSource::cubemap);
        expect(eq(binding->asset, std::string{"sky.png"}));
        expect(binding->filter == ChannelFilter::mipmap);
        expect(binding->wrap == ChannelWrap::clamp);
        expect(!binding->vflip);
    };

    "type volume and type image set the source of an asset"_test = [] {
        const auto volume = fileBindingOf("cloud.dat type=volume");
        expect(volume.has_value());
        expect(volume->source == ChannelSource::volume);
        expect(volume->filter == ChannelFilter::linear);
        expect(!volume->vflip);

        const auto image = fileBindingOf("grain.bin type=image");
        expect(image.has_value());
        expect(image->source == ChannelSource::image);
        expect(eq(image->asset, std::string{"grain.bin"}));
    };

    "explicit options override the defaults of an image"_test = [] {
        const auto binding = fileBindingOf("noise.png type=image filter=nearest wrap=clamp vflip=false");
        expect(binding.has_value());
        expect(binding->source == ChannelSource::image);
        expect(binding->filter == ChannelFilter::nearest);
        expect(binding->wrap == ChannelWrap::clamp);
        expect(!binding->vflip);
    };

    "every filter and wrap name is accepted"_test = [] {
        expect(fileBindingOf("a.png filter=nearest")->filter == ChannelFilter::nearest);
        expect(fileBindingOf("a.png filter=linear")->filter == ChannelFilter::linear);
        expect(fileBindingOf("a.png filter=mipmap")->filter == ChannelFilter::mipmap);
        expect(fileBindingOf("a.png wrap=clamp")->wrap == ChannelWrap::clamp);
        expect(fileBindingOf("a.png wrap=repeat")->wrap == ChannelWrap::repeat);
    };

    "a bare vflip and vflip true flip, vflip false does not"_test = [] {
        expect(fileBindingOf("grey.bin vflip")->vflip);
        expect(fileBindingOf("grey.bin vflip=true")->vflip);
        expect(!fileBindingOf("grey.bin vflip=false")->vflip);
        expect(!fileBindingOf("a.png vflip=false")->vflip);
        expect(fileBindingOf("BufferA vflip")->vflip);
    };

    "options on a buffer target override its defaults"_test = [] {
        const auto binding = fileBindingOf("BufferB filter=nearest wrap=repeat");
        expect(binding.has_value());
        expect(binding->source == ChannelSource::pass);
        expect(binding->filter == ChannelFilter::nearest);
        expect(binding->wrap == ChannelWrap::repeat);
    };

    "the order of options does not change the binding"_test = [] {
        const auto filterFirst = fileBindingOf("sky.png filter=nearest type=cubemap");
        const auto typeFirst   = fileBindingOf("sky.png type=cubemap filter=nearest");
        expect(typeFirst.has_value() && typeFirst->filter == ChannelFilter::nearest);
        expect(filterFirst.has_value() && filterFirst->filter == ChannelFilter::nearest) << "type=cubemap must not discard an earlier filter";
        expect(filterFirst == typeFirst);
    };

    "a bad channel option is warned about with its line and the binding is ignored"_test = [] {
        const std::vector<std::string_view> bad{
            "a.png filter=sharp",
            "a.png wrap=mirror",
            "a.png type=video",
            "a.png vflip=maybe",
            "a.png colour=red",
            "a.png nearest",
            "BufferA type=image",
            "",
        };
        for (const std::string_view arguments : bad) {
            const ParsedEffect parsed = parsedOk(withImage({std::string{"// @channel0 "} + std::string{arguments}}));
            expect(eq(parsed.warnings.size(), 1UZ)) << arguments;
            expect(hasWarning(parsed, "fx:1")) << arguments;
            expect(!parsed.effect.channels[0].has_value()) << arguments;
        }
    };

    "each channel number binds its own slot"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @channel0 zero.png", "// @channel1 one.png", "// @channel2 two.png", "// @channel3 three.png"}));
        expect(parsed.warnings.empty());
        expect(eq(parsed.effect.channels[0]->asset, std::string{"zero.png"}));
        expect(eq(parsed.effect.channels[1]->asset, std::string{"one.png"}));
        expect(eq(parsed.effect.channels[2]->asset, std::string{"two.png"}));
        expect(eq(parsed.effect.channels[3]->asset, std::string{"three.png"}));
    };

    "a header binding is file-scope and a binding after a pass marker belongs to that pass"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @channel0 file.png", "//--- pass: Image", "// @channel1 pass.png", kImageCode}));
        expect(parsed.warnings.empty());
        const EffectPass* image = parsed.effect.pass(PassKind::image);
        expect(image != nullptr);
        expect(parsed.effect.channels[0].has_value());
        expect(!parsed.effect.channels[1].has_value());
        expect(!image->channels[0].has_value());
        expect(image->channels[1].has_value());
        expect(eq(image->channels[1]->asset, std::string{"pass.png"}));
    };

    "a pass binding overrides the file binding for its channel and the file binding serves the other channels"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @channel0 file0.png", "// @channel1 file1.png", "//--- pass: Image", "// @channel0 pass0.png", kImageCode}));
        const EffectPass&  image  = *parsed.effect.pass(PassKind::image);
        expect(eq(parsed.effect.binding(image, 0UZ)->asset, std::string{"pass0.png"}));
        expect(eq(parsed.effect.binding(image, 1UZ)->asset, std::string{"file1.png"}));
        expect(!parsed.effect.binding(image, 2UZ).has_value());
        expect(!parsed.effect.binding(image, 3UZ).has_value());
        expect(!parsed.effect.binding(image, 4UZ).has_value());
        expect(eq(parsed.effect.channels[0]->asset, std::string{"file0.png"})) << "the file binding itself is unchanged";
    };

    "a pass binding does not leak to another pass"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"//--- pass: BufferA", "// @channel0 only-a.png", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }", "//--- pass: Image", kImageCode}));
        expect(parsed.effect.binding(*parsed.effect.pass(PassKind::bufferA), 0UZ).has_value());
        expect(!parsed.effect.binding(*parsed.effect.pass(PassKind::image), 0UZ).has_value());
    };

    "a channel line inside a pass section binds that pass even after code"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"//--- pass: Image", "void mainImage(out vec4 c, in vec2 f) {", "// @channel0 late.png", "    c = vec4(1.0);", "}"}));
        const EffectPass&  image  = *parsed.effect.pass(PassKind::image);
        expect(image.channels[0].has_value());
        expect(eq(image.channels[0]->asset, std::string{"late.png"}));
    };

    "a channel line after the first code line of an unmarked file still reaches the Image pass"_test = [] {
        const ParsedEffect parsed    = parsedOk(joined({kImageCode, "// @channel0 late.png"}));
        const auto         effective = parsed.effect.binding(*parsed.effect.pass(PassKind::image), 0UZ);
        expect(effective.has_value()) << "the whole unmarked file is the Image pass section";
        expect(effective.has_value() && effective->asset == "late.png");
    };

    "a buffer binding to a pass the effect does not define is warned about"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @channel0 BufferA"}));
        expect(eq(parsed.warnings.size(), 1UZ));
        expect(hasWarning(parsed, "fx:"));
    };

    "a CubeA binding to an effect without a CubeA pass is warned about"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @channel2 CubeA"}));
        expect(eq(parsed.warnings.size(), 1UZ));
    };

    "a pass binding to an undefined buffer is warned about and a defined one is not"_test = [] {
        const ParsedEffect undefined = parsedOk(joined({"//--- pass: Image", "// @channel0 BufferB", kImageCode}));
        expect(eq(undefined.warnings.size(), 1UZ));

        const ParsedEffect defined = parsedOk(joined({"//--- pass: BufferB", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }", "//--- pass: Image", "// @channel0 BufferB", kImageCode}));
        expect(defined.warnings.empty());
    };

    "bindings to defined buffers and cube passes raise no warning"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @channel0 BufferA", "// @channel1 CubeA", "//--- pass: BufferA", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }", "//--- pass: CubeA", "void mainCubemap(out vec4 c, in vec2 f, in vec3 o, in vec3 d) { c = vec4(d, 1.0); }", "//--- pass: Image", kImageCode}));
        expect(parsed.warnings.empty());
    };
};

const suite<"EffectSource passes"> passTests = [] {
    "a file without markers is the Image pass starting at line one"_test = [] {
        const std::string  text   = joined({"// @name plain", kImageCode, "float helper = 1.0;"});
        const ParsedEffect parsed = parsedOk(text);
        expect(parsed.warnings.empty());
        expect(eq(parsed.effect.passes.size(), 1UZ));
        expect(parsed.effect.passes[0].kind == PassKind::image);
        expect(eq(parsed.effect.passes[0].firstLine, 1UZ));
        expect(eq(parsed.effect.passes[0].code, text));
    };

    "markers split the file and each pass starts on the line after its marker"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({
            "// @name multi",                                                   // 1
            "//--- pass: Common",                                               // 2
            "float shared(float x) { return x * 2.0; }",                        // 3
            "//--- pass: BufferA",                                              // 4
            "void mainImage(out vec4 c, in vec2 f) { c = vec4(shared(f.x)); }", // 5
            "// second buffer line",                                            // 6
            "//--- pass: Image",                                                // 7
            "",                                                                 // 8
            kImageCode,                                                         // 9
        }));
        expect(parsed.warnings.empty());
        expect(eq(parsed.effect.passes.size(), 3UZ));

        const EffectPass& common = parsed.effect.passes[0];
        expect(common.kind == PassKind::common);
        expect(eq(common.firstLine, 3UZ));
        expect(common.code.contains("float shared(float x)"));
        expect(!common.code.contains("mainImage"));
        expect(!common.code.contains("//---"));

        const EffectPass& buffer = parsed.effect.passes[1];
        expect(buffer.kind == PassKind::bufferA);
        expect(eq(buffer.firstLine, 5UZ));
        expect(buffer.code.contains("shared(f.x)"));
        expect(buffer.code.contains("// second buffer line"));
        expect(!buffer.code.contains("float shared(float x)"));
        expect(!buffer.code.contains("//---"));

        const EffectPass& image = parsed.effect.passes[2];
        expect(image.kind == PassKind::image);
        expect(eq(image.firstLine, 8UZ));
        expect(image.code.contains("colour = vec4(1.0)"));
        expect(!image.code.contains("shared"));
    };

    "passes are sorted Common, BufferA to D, CubeA, Image whatever order they are written in"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"//--- pass: Image", kImageCode, "//--- pass: CubeA", "void mainCubemap(out vec4 c, in vec2 f, in vec3 o, in vec3 d) { c = vec4(d, 1.0); }", "//--- pass: BufferD", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }", "//--- pass: BufferB", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }", "//--- pass: Common", "const float k = 1.0;", "//--- pass: BufferC", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }", "//--- pass: BufferA", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }"}));
        expect(parsed.warnings.empty());
        std::vector<PassKind> kinds;
        for (const EffectPass& effectPass : parsed.effect.passes) {
            kinds.push_back(effectPass.kind);
        }
        expect(kinds == std::vector<PassKind>{PassKind::common, PassKind::bufferA, PassKind::bufferB, PassKind::bufferC, PassKind::bufferD, PassKind::cubeA, PassKind::image});
    };

    "firstLine stays the file line of a pass that sorting moved"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"//--- pass: Image", kImageCode, "//--- pass: Common", "const float k = 1.0;"}));
        expect(eq(parsed.effect.pass(PassKind::image)->firstLine, 2UZ));
        expect(eq(parsed.effect.pass(PassKind::common)->firstLine, 4UZ));
    };

    "pass lookup returns the pass or nothing"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"//--- pass: Image", kImageCode}));
        expect(parsed.effect.pass(PassKind::image) != nullptr);
        expect(parsed.effect.pass(PassKind::bufferA) == nullptr);
        expect(parsed.effect.pass(PassKind::common) == nullptr);
    };

    "an unknown pass name is warned about with its line and its section is ignored"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"//--- pass: Image", kImageCode, "//--- pass: BufferZ", "float leaked = 1.0;"}));
        expect(eq(parsed.warnings.size(), 1UZ));
        expect(hasWarning(parsed, "fx:3"));
        expect(eq(parsed.effect.passes.size(), 1UZ));
        expect(!parsed.effect.pass(PassKind::image)->code.contains("leaked"));
    };

    "a duplicate pass is warned about and the first section is kept"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"//--- pass: Image", kImageCode, "//--- pass: Image", "float second = 1.0;", "//--- pass: Common", "float common = 1.0;"}));
        expect(eq(parsed.warnings.size(), 1UZ));
        expect(hasWarning(parsed, "fx:3"));
        expect(eq(parsed.effect.passes.size(), 2UZ));
        const EffectPass& image = *parsed.effect.pass(PassKind::image);
        expect(image.code.contains("colour = vec4(1.0)"));
        expect(!image.code.contains("second"));
        expect(eq(image.firstLine, 2UZ));
        expect(!parsed.effect.pass(PassKind::common)->code.contains("second"));
    };

    "code before the first marker is warned about with its line and kept out of every pass"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @name stray", "// a comment is fine", "", "float stray = 1.0;", "//--- pass: Image", kImageCode}));
        expect(eq(parsed.warnings.size(), 1UZ));
        expect(hasWarning(parsed, "fx:4"));
        expect(!parsed.effect.pass(PassKind::image)->code.contains("stray"));
    };

    "comments and blank lines before the first marker raise no warning"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @name tidy", "", "// prose", "//--- pass: Image", kImageCode}));
        expect(parsed.warnings.empty());
    };

    "a marker that is not a pass marker is an ordinary comment"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"//--- section", kImageCode}));
        expect(parsed.warnings.empty());
        expect(eq(parsed.effect.passes.size(), 1UZ));
        expect(eq(parsed.effect.passes[0].firstLine, 1UZ));
    };

    "an effect without an Image pass fails"_test = [] {
        const auto onlyBuffer = parseEffect("fx", joined({"//--- pass: BufferA", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }"}));
        expect(!onlyBuffer.has_value());
        expect(onlyBuffer.error().contains("fx"));
        const auto onlyUnknown = parseEffect("fx", joined({"//--- pass: Nothing", "float x = 1.0;"}));
        expect(!onlyUnknown.has_value());
    };

    "an Image pass of only whitespace fails"_test = [] {
        expect(!parseEffect("fx", joined({"//--- pass: Image", "   ", "\t", ""})).has_value());
        expect(!parseEffect("fx", joined({"//--- pass: Image"})).has_value());
        expect(!parseEffect("fx", joined({"//--- pass: BufferA", "float a;", "//--- pass: Image", "", "  "})).has_value());
    };

    "an empty file and a whitespace-only file fail"_test = [] {
        expect(!parseEffect("fx", "").has_value());
        expect(!parseEffect("fx", "\n\n  \t\n").has_value());
    };

    "a file with a last line lacking a newline parses the same as one with it"_test = [] {
        const ParsedEffect withNewline    = parsedOk(joined({"//--- pass: Image", kImageCode}));
        const ParsedEffect withoutNewline = parsedOk(std::string{"//--- pass: Image\n"} + std::string{kImageCode});
        expect(eq(withNewline.effect.passes.size(), withoutNewline.effect.passes.size()));
        expect(eq(withNewline.effect.passes[0].firstLine, withoutNewline.effect.passes[0].firstLine));
        expect(withoutNewline.effect.passes[0].code.contains("colour = vec4(1.0)"));
    };
};

const suite<"EffectSource line endings"> lineEndingTests = [] {
    "a marked file with CRLF endings parses like its LF form"_test = [] {
        const std::string  lf       = joined({
            "// @name crlf",
            "// @author Ada",
            "// @uses background reveal",
            "// @param speed float 1.0 0.1 10.0",
            "// @param glow colour #ff8000",
            "// @channel0 BufferA",
            "// @bogus nope",
            "//--- pass: Common",
            "const float k = 1.0;",
            "//--- pass: BufferA",
            "// @channel1 wood.png",
            "void mainImage(out vec4 c, in vec2 f) { c = vec4(k); }",
            "//--- pass: Image",
            kImageCode,
        });
        const ParsedEffect fromLf   = parsedOk(lf);
        const ParsedEffect fromCrlf = parsedOk(crlfOf(lf));
        expect(eq(fromLf.warnings.size(), 1UZ));
        expect(fromCrlf.warnings == fromLf.warnings);
        expect(eq(fromCrlf.effect.name, fromLf.effect.name));
        expect(eq(fromCrlf.effect.author, fromLf.effect.author));
        expect(fromCrlf.effect.uses == fromLf.effect.uses);
        expect(fromCrlf.effect.parameters == fromLf.effect.parameters);
        expect(fromCrlf.effect.channels == fromLf.effect.channels);
        expect(eq(fromCrlf.effect.passes.size(), fromLf.effect.passes.size()));
        for (std::size_t index = 0UZ; index < fromLf.effect.passes.size(); ++index) {
            expect(fromCrlf.effect.passes[index].kind == fromLf.effect.passes[index].kind);
            expect(eq(fromCrlf.effect.passes[index].firstLine, fromLf.effect.passes[index].firstLine));
            expect(fromCrlf.effect.passes[index].channels == fromLf.effect.passes[index].channels);
            expect(fromCrlf.effect.passes[index].code.find('\r') == std::string::npos) << "a pass carries no carriage return";
            expect(eq(fromCrlf.effect.passes[index].code, fromLf.effect.passes[index].code));
        }
    };

    "an unmarked file with CRLF endings reads its header and bindings like LF"_test = [] {
        const std::string  lf       = withImage({"// @name crlf", "// @uses overlay", "// @param speed float 2.0", "// @channel0 wood.png filter=nearest"});
        const ParsedEffect fromLf   = parsedOk(lf);
        const ParsedEffect fromCrlf = parsedOk(crlfOf(lf));
        expect(fromCrlf.warnings.empty());
        expect(eq(fromCrlf.effect.name, std::string{"crlf"}));
        expect(fromCrlf.effect.uses == fromLf.effect.uses);
        expect(fromCrlf.effect.parameters == fromLf.effect.parameters);
        expect(fromCrlf.effect.channels == fromLf.effect.channels);
        expect(eq(fromCrlf.effect.passes.size(), 1UZ));
        expect(eq(fromCrlf.effect.passes[0].firstLine, 1UZ));
    };

    "warning lines count CRLF line breaks once"_test = [] {
        const ParsedEffect parsed = parsedOk(crlfOf(withImage({"// @author Ada", "// @bogus value"})));
        expect(eq(parsed.warnings.size(), 1UZ));
        expect(hasWarning(parsed, "fx:2"));
    };
};

const suite<"EffectSource names"> nameTests = [] {
    "every use round-trips through its name and break is the pause use"_test = [] {
        for (const EffectUse use : {EffectUse::transition, EffectUse::background, EffectUse::viewport, EffectUse::overlay, EffectUse::reveal, EffectUse::camera, EffectUse::pointer, EffectUse::pause}) {
            const auto parsed = parseEffectUse(name(use));
            expect(parsed.has_value() && *parsed == use) << name(use);
        }
        expect(eq(name(EffectUse::pause), std::string_view{"break"}));
        expect(parseEffectUse("break") == std::optional{EffectUse::pause});
        expect(eq(name(EffectUse::transition), std::string_view{"transition"}));
        expect(!parseEffectUse("pause").has_value());
        expect(!parseEffectUse("").has_value());
    };

    "every pass kind round-trips through its name"_test = [] {
        for (const PassKind kind : {PassKind::common, PassKind::bufferA, PassKind::bufferB, PassKind::bufferC, PassKind::bufferD, PassKind::cubeA, PassKind::image}) {
            const auto parsed = parsePassKind(name(kind));
            expect(parsed.has_value() && *parsed == kind) << name(kind);
        }
        expect(eq(name(PassKind::common), std::string_view{"Common"}));
        expect(eq(name(PassKind::bufferD), std::string_view{"BufferD"}));
        expect(eq(name(PassKind::cubeA), std::string_view{"CubeA"}));
        expect(eq(name(PassKind::image), std::string_view{"Image"}));
        expect(!parsePassKind("BufferE").has_value());
        expect(!parsePassKind("image").has_value());
    };

    "every parameter type round-trips through its name"_test = [] {
        const std::vector<std::pair<ParameterType, std::string_view>> types{{ParameterType::scalar, "float"}, {ParameterType::integer, "int"}, {ParameterType::boolean, "bool"}, {ParameterType::vec2, "vec2"}, {ParameterType::vec3, "vec3"}, {ParameterType::vec4, "vec4"}, {ParameterType::colour, "colour"}};
        for (const auto& [type, typeName] : types) {
            const auto parsed = parseParameterType(typeName);
            expect(parsed.has_value() && *parsed == type) << typeName;
        }
        expect(!parseParameterType("vec5").has_value());
        expect(!parseParameterType("color").has_value());
    };
};

const suite<"EffectSource assets"> assetTests = [] {
    "buffers and a pass-less effect contribute no assets"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @channel0 BufferA", "// @channel1 CubeA", "//--- pass: BufferA", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }", "//--- pass: CubeA", "void mainCubemap(out vec4 c, in vec2 f, in vec3 o, in vec3 d) { c = vec4(d, 1.0); }", "//--- pass: Image", kImageCode}));
        expect(effectAssets(parsed.effect).empty());
        expect(effectAssets(EffectSource{}).empty());
    };

    "images and volumes are listed once each in file-then-pass order"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @channel0 wood.png", "// @channel1 noise.bin", "// @channel2 wood.png", "//--- pass: Image", "// @channel3 extra/grain.jpg", "// @channel0 wood.png", kImageCode}));
        expect(effectAssets(parsed.effect) == std::vector<std::string>{"wood.png", "noise.bin", "extra/grain.jpg"});
    };

    "a pass binding that overrides a file binding still lists both assets"_test = [] {
        const ParsedEffect parsed = parsedOk(joined({"// @channel0 file.png", "//--- pass: Image", "// @channel0 pass.png", kImageCode}));
        expect(effectAssets(parsed.effect) == std::vector<std::string>{"file.png", "pass.png"});
    };

    "a cubemap expands to its six faces"_test = [] {
        const ParsedEffect parsed = parsedOk(withImage({"// @channel0 sky.png type=cubemap"}));
        expect(effectAssets(parsed.effect) == std::vector<std::string>{"sky.png", "sky_1.png", "sky_2.png", "sky_3.png", "sky_4.png", "sky_5.png"});
    };

    "cubemap faces keep the directory and a dot in a directory is not an extension"_test = [] {
        const ParsedEffect directory = parsedOk(withImage({"// @channel0 env/day.jpg type=cubemap"}));
        expect(effectAssets(directory.effect) == std::vector<std::string>{"env/day.jpg", "env/day_1.jpg", "env/day_2.jpg", "env/day_3.jpg", "env/day_4.jpg", "env/day_5.jpg"});
        const ParsedEffect dotted = parsedOk(withImage({"// @channel0 my.maps/sky type=cubemap"}));
        expect(effectAssets(dotted.effect) == std::vector<std::string>{"my.maps/sky", "my.maps/sky_1", "my.maps/sky_2", "my.maps/sky_3", "my.maps/sky_4", "my.maps/sky_5"});
    };

    "a cubemap bound twice, or overlapping an image face, lists each file once"_test = [] {
        const ParsedEffect             parsed = parsedOk(joined({"// @channel0 sky.png type=cubemap", "// @channel1 sky.png type=cubemap filter=nearest", "// @channel2 sky_3.png", "//--- pass: Image", "// @channel0 sky.png type=cubemap", kImageCode}));
        const std::vector<std::string> assets = effectAssets(parsed.effect);
        expect(assets == std::vector<std::string>{"sky.png", "sky_1.png", "sky_2.png", "sky_3.png", "sky_4.png", "sky_5.png"});
    };

    "assets of an effect text match the assets of the parsed effect"_test = [] {
        const std::string text = joined({"// @channel0 sky.png type=cubemap", "// @channel1 wood.png", "// @channel2 BufferA", "//--- pass: BufferA", "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.0); }", "//--- pass: Image", "// @channel3 noise.bin", kImageCode});
        expect(effectAssetsOf(text) == effectAssets(parsedOk(text).effect));
        expect(eq(effectAssetsOf(text).size(), 8UZ));
    };

    "assets of an effect text are found without a header or with CRLF endings"_test = [] {
        const std::string text = withImage({"// @channel0 wood.png"});
        expect(effectAssetsOf(text) == std::vector<std::string>{"wood.png"});
        expect(effectAssetsOf(crlfOf(text)) == std::vector<std::string>{"wood.png"});
    };

    "assets of an unparseable effect text are empty"_test = [] {
        expect(effectAssetsOf("").empty());
        expect(effectAssetsOf(joined({"// @channel0 wood.png", "//--- pass: BufferA", "float a;"})).empty());
    };
};

int main() { return 0; }
