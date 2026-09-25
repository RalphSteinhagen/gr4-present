#include <boost/ut.hpp>

#include <gr4-present/LiveRegion.hpp>

using namespace boost::ut;
using namespace gr::present;

const suite<"LiveRegion"> liveRegionTests = [] {
    "lifecycle policies round-trip through their document names"_test = [] {
        for (const LifecyclePolicy policy : {LifecyclePolicy::preload, LifecyclePolicy::lazy, LifecyclePolicy::startOnEnter, LifecyclePolicy::runWhileVisible, LifecyclePolicy::keepAlive, LifecyclePolicy::resetOnEnter, LifecyclePolicy::persistent}) {
            const std::string_view policyName = name(policy);
            expect(!policyName.empty());
            const auto parsed = parseLifecyclePolicy(policyName);
            expect(parsed.has_value()) << policyName;
            expect(*parsed == policy) << policyName;
        }
    };

    "unknown lifecycle policy is rejected rather than defaulted"_test = [] {
        expect(!parseLifecyclePolicy("run-forever").has_value());
        expect(!parseLifecyclePolicy("").has_value());
    };

    "UI categories are selected by their reflected GR4 names"_test = [] {
        expect(parseUICategory("Content") == std::optional{gr::UICategory::Content});
        expect(parseUICategory("Panel") == std::optional{gr::UICategory::Panel});
        expect(parseUICategory("Toolbar") == std::optional{gr::UICategory::Toolbar});
        expect(parseUICategory("Overlay") == std::optional{gr::UICategory::Overlay});
        expect(parseUICategory("StatusBar") == std::optional{gr::UICategory::StatusBar});
    };

    "unknown or differently cased UI category names are rejected"_test = [] {
        expect(!parseUICategory("content").has_value());
        expect(!parseUICategory("Spectrum").has_value());
    };

    "a region defaults to a lazily started live binding"_test = [] {
        const LiveRegion region{.workflow = "workflows/demo.grc", .widget = "spectrum-main", .category = std::nullopt, .region = "#spectrum", .fallback = "fallback/spectrum.png", .step = 3UZ, .lifecycle = LifecyclePolicy::lazy, .mode = RenderMode::live};
        expect(region.lifecycle == LifecyclePolicy::lazy);
        expect(region.mode == RenderMode::live);
        expect(!region.category.has_value());
    };

    "a region may select its contribution by category instead of id"_test = [] {
        const LiveRegion region{.workflow = "workflows/demo.grc", .widget = {}, .category = parseUICategory("Panel"), .region = "#controls", .fallback = {}, .step = 0UZ, .lifecycle = LifecyclePolicy::startOnEnter, .mode = RenderMode::live};
        expect(region.widget.empty());
        expect(region.category == std::optional{gr::UICategory::Panel});
    };
};

int main() { return 0; }
