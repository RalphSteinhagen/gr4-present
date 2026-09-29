#include <boost/ut.hpp>

#include <gr4-present/RegionGeometry.hpp>

using namespace boost::ut;
using namespace gr::present;

namespace {
[[nodiscard]] LiveRegion spectrumBinding() { return LiveRegion{.workflow = "workflows/demo.grc", .widget = "spectrum-main", .region = "#spectrum", .fallback = {}}; }
} // namespace

const suite<"RegionGeometry"> regionGeometryTests = [] {
    "an unbound region is not found"_test = [] {
        const RegionRegistry registry;
        expect(registry.find("#spectrum") == nullptr);
    };

    "rebinding a region id replaces the binding"_test = [] {
        RegionRegistry registry;
        registry.bind("#spectrum", spectrumBinding());

        LiveRegion replacement = spectrumBinding();
        replacement.widget     = "spectrum-detail";
        registry.bind("#spectrum", replacement);

        expect(eq(registry.entries.size(), 1UZ));
        const RegionRegistry::Entry* entry = registry.find("#spectrum");
        expect(fatal(entry != nullptr));
        expect(eq(entry->binding.widget, std::string{"spectrum-detail"}));
    };

    "several logical regions share one registry"_test = [] {
        RegionRegistry registry;
        registry.bind("#spectrum", spectrumBinding());

        LiveRegion controls = spectrumBinding();
        controls.widget     = {};
        controls.region     = "#controls";
        registry.bind("#controls", controls);

        expect(eq(registry.entries.size(), 2UZ));
        expect(registry.find("#controls") != nullptr);
        expect(registry.find("#spectrum") != nullptr);
        expect(registry.find("#missing") == nullptr);
    };
};

int main() { return 0; }
