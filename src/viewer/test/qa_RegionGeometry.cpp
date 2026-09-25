#include <boost/ut.hpp>

#include "RegionGeometry.hpp"

using namespace boost::ut;
using namespace gr::present;

namespace {
[[nodiscard]] LiveRegion spectrumBinding() { return LiveRegion{.workflow = "workflows/demo.grc", .widget = "spectrum-main", .category = std::nullopt, .region = "#spectrum", .fallback = {}, .step = 0UZ, .lifecycle = LifecyclePolicy::lazy, .mode = RenderMode::live}; }
} // namespace

const suite<"RegionGeometry"> regionGeometryTests = [] {
    "an unbound region is not found"_test = [] {
        const RegionRegistry registry;
        expect(registry.find("#spectrum") == nullptr);
    };

    "geometry forwarded for an unbound region is rejected"_test = [] {
        RegionRegistry registry;
        expect(!registry.updateGeometry("#spectrum", RegionGeometry{}));
    };

    "browser-computed geometry reaches the bound region"_test = [] {
        RegionRegistry registry;
        registry.bind("#spectrum", spectrumBinding());

        const RegionGeometry geometry{.bounds = {.x = 10.0f, .y = 20.0f, .width = 640.0f, .height = 480.0f}, .clip = {.x = 0.0f, .y = 0.0f, .width = 1280.0f, .height = 720.0f}, .opacity = 0.5f, .visible = true};
        expect(registry.updateGeometry("#spectrum", geometry));

        const RegionRegistry::Entry* entry = registry.find("#spectrum");
        expect(entry != nullptr);
        expect(entry->geometry == geometry);
        expect(eq(entry->binding.widget, std::string{"spectrum-main"}));
    };

    "rebinding a region id replaces the binding and keeps the geometry"_test = [] {
        RegionRegistry registry;
        registry.bind("#spectrum", spectrumBinding());
        expect(registry.updateGeometry("#spectrum", RegionGeometry{.bounds = {.x = 1.0f, .y = 2.0f, .width = 3.0f, .height = 4.0f}, .clip = {}, .opacity = 1.0f, .visible = true}));

        LiveRegion replacement = spectrumBinding();
        replacement.widget     = "spectrum-detail";
        registry.bind("#spectrum", replacement);

        expect(eq(registry.entries.size(), 1UZ));
        const RegionRegistry::Entry* entry = registry.find("#spectrum");
        expect(entry != nullptr);
        expect(eq(entry->binding.widget, std::string{"spectrum-detail"}));
        expect(eq(entry->geometry.bounds.width, 3.0f));
    };

    "several logical regions share one registry"_test = [] {
        RegionRegistry registry;
        registry.bind("#spectrum", spectrumBinding());

        LiveRegion controls = spectrumBinding();
        controls.widget     = {};
        controls.category   = gr::UICategory::Panel;
        controls.region     = "#controls";
        registry.bind("#controls", controls);

        expect(eq(registry.entries.size(), 2UZ));
        expect(registry.find("#controls") != nullptr);
        expect(registry.find("#spectrum") != nullptr);
        expect(registry.find("#missing") == nullptr);
    };

    "a hidden region keeps its binding"_test = [] {
        RegionRegistry registry;
        registry.bind("#spectrum", spectrumBinding());
        expect(registry.updateGeometry("#spectrum", RegionGeometry{.bounds = {}, .clip = {}, .opacity = 0.0f, .visible = false}));

        const RegionRegistry::Entry* entry = registry.find("#spectrum");
        expect(entry != nullptr);
        expect(!entry->geometry.visible);
        expect(eq(entry->binding.workflow, std::string{"workflows/demo.grc"}));
    };
};

int main() { return 0; }
