#include <boost/ut.hpp>

#include <gr4-present/LiveRegion.hpp>

using namespace boost::ut;
using namespace gr::present;

const suite<"LiveRegion"> liveRegionTests = [] {
    "a binding is built from a gr4 block's fields"_test = [] {
        const std::vector<std::pair<std::string, std::string>> fields{{"workflow", "workflows/demo.grc"}, {"widget", "FFT Spectrum"}, {"region", "#spectrum"}, {"fallback", "fallback/spectrum.png"}, {"lifecycle", "run-while-visible"}};
        const LiveRegion                                       binding = liveRegionFrom(fields);
        expect(eq(binding.workflow, std::string{"workflows/demo.grc"}));
        expect(eq(binding.widget, std::string{"FFT Spectrum"}));
        expect(eq(binding.region, std::string{"#spectrum"}));
        expect(eq(binding.fallback, std::string{"fallback/spectrum.png"}));
    };

    "an empty field list yields an empty binding rather than failing"_test = [] {
        const LiveRegion binding = liveRegionFrom({});
        expect(binding.workflow.empty());
        expect(binding.widget.empty());
    };

    "a live region names a standby workflow and the device the main one needs"_test = [] {
        const LiveRegion region = liveRegionFrom({{"workflow", "workflows/microphone.grc"}, {"needs", "audio"}, {"standby", "workflows/clap.grc"}});
        expect(region.workflow == "workflows/microphone.grc");
        expect(region.needs == "audio");
        expect(region.standby == "workflows/clap.grc");
    };

    "a live region may run longer or shorter than the deck before an exported page takes it"_test = [] {
        expect(std::abs(liveRegionFrom({{"workflow", "w.grc"}, {"export_wait", "7s"}}).exportWait - 7.0f) < 0.001f);
        expect(std::abs(liveRegionFrom({{"workflow", "w.grc"}, {"export_wait", "0.5"}}).exportWait - 0.5f) < 0.001f);
        expect(liveRegionFrom({{"workflow", "w.grc"}}).exportWait < 0.0f) << "negative: the deck's live_wait applies";
        expect(liveRegionFrom({{"workflow", "w.grc"}, {"export_wait", "soon"}}).exportWait < 0.0f) << "nonsense is not a wait";
    };

    "a live region may put its legend elsewhere than the deck does, or leave it to the deck"_test = [] {
        expect(liveRegionFrom({{"workflow", "w.grc"}, {"legend", "top"}}).legend == std::optional{ChartLegend::top});
        expect(liveRegionFrom({{"workflow", "w.grc"}, {"legend", "left"}}).legend == std::optional{ChartLegend::left});
        expect(liveRegionFrom({{"workflow", "w.grc"}, {"legend", "none"}}).legend == std::optional{ChartLegend::none});
        expect(!liveRegionFrom({{"workflow", "w.grc"}}).legend.has_value()) << "unset: the deck's applies";
        expect(!liveRegionFrom({{"workflow", "w.grc"}, {"legend", "middle"}}).legend.has_value()) << "nonsense is reported and the deck's applies";
    };

    "a live region asks for a toolbar and a status bar beside its charts or in an area of its own"_test = [] {
        const LiveRegion beside = liveRegionFrom({{"workflow", "w.grc"}, {"toolbar", "here"}, {"status", "here"}});
        expect(beside.toolbar == "here" && beside.status == "here");
        const LiveRegion apart = liveRegionFrom({{"workflow", "w.grc"}, {"toolbar", "band"}});
        expect(apart.toolbar == "band" && apart.status.empty()) << "a status bar is asked for, never implied";
        expect(liveRegionFrom({{"workflow", "w.grc"}}).toolbar.empty());
    };

    "a relative uri in a workflow is joined onto the package it came with"_test = [] {
        const std::string workflow = "blocks:\n  - id: WavSource\n    parameters:\n      uri: media/clap.wav\n      repeat: true\n";
        expect(withPackageUris(workflow, "https://host/deck/default") == "blocks:\n  - id: WavSource\n    parameters:\n      uri: \"https://host/deck/default/media/clap.wav\"\n      repeat: true\n");
        expect(withPackageUris(workflow, "/opt/talk/default/").contains("uri: \"/opt/talk/default/media/clap.wav\""));
    };

    "a quoted relative uri is joined too"_test = [] { expect(withPackageUris("      uri: 'media/a b.wav'", "base") == "      uri: \"base/media/a b.wav\""); };

    "absolute paths and uris with a scheme are left as written"_test = [] {
        for (const std::string_view line : {"  uri: /data/clap.wav", "  uri: https://example.org/clap.wav", "  uri: file:/tmp/clap.wav", "  uri: \"http://x/y.wav\"", "  uri:", "  name: media/clap.wav"}) {
            expect(withPackageUris(line, "https://host/default") == line) << line;
        }
    };
};

int main() { return 0; }
