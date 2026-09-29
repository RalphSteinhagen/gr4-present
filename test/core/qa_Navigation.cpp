#include <boost/ut.hpp>

#include <gr4-present/Navigation.hpp>

using namespace boost::ut;
using namespace gr::present;

namespace {
[[nodiscard]] NavigationGraph linearGraph() {
    return NavigationGraph{.views = {
                               View{.id = "intro", .stepCount = 1UZ, .next = {}},
                               View{.id = "architecture", .stepCount = 3UZ, .next = {}},
                               View{.id = "demo", .stepCount = 1UZ, .next = {}},
                           }};
}
} // namespace

const suite<"Navigation"> navigationTests = [] {
    "an edge list is split on commas and trimmed"_test = [] {
        const auto edges = parseEdgeList("architecture, demo ,  summary");
        expect(eq(edges.size(), 3UZ));
        expect(eq(edges.at(0UZ), std::string{"architecture"}));
        expect(eq(edges.at(1UZ), std::string{"demo"})) << "surrounding space is not part of an id";
        expect(eq(edges.at(2UZ), std::string{"summary"}));
    };

    "an empty or blank list yields no edges rather than empty ids"_test = [] {
        expect(parseEdgeList("").empty());
        expect(parseEdgeList("   ").empty());
        expect(parseEdgeList(",,").empty()) << "a blank entry is not a view called nothing";
        expect(eq(parseEdgeList("a,,b").size(), 2UZ));
    };

    "next view skips the steps still to come, and back returns to the step it left"_test = [] {
        Navigator navigator;
        navigator.graph.views = {View{.id = "a", .stepCount = 3UZ, .next = {}}, View{.id = "b", .stepCount = 1UZ, .next = {}}};
        navigator.cursor      = Cursor{.viewId = "a", .step = 1UZ};
        expect(navigator.nextView());
        expect(eq(navigator.cursor.viewId, std::string{"b"}) && eq(navigator.cursor.step, 0UZ)) << "step 2 of a is skipped";
        expect(navigator.previous());
        expect(eq(navigator.cursor.viewId, std::string{"a"}) && eq(navigator.cursor.step, 1UZ)) << "back is where it was";
        navigator.cursor = Cursor{.viewId = "b", .step = 0UZ};
        expect(!navigator.nextView()) << "the last view has nowhere to go";
    };

    "a branch follows its first edge, and the others are reachable by name"_test = [] {
        Navigator navigator;
        navigator.graph.views = {View{.id = "a", .stepCount = 1UZ, .next = {"b", "c"}}, View{.id = "b", .stepCount = 1UZ, .next = {"d"}}, View{.id = "c", .stepCount = 1UZ, .next = {"d"}}, View{.id = "d", .stepCount = 1UZ, .next = {}}};
        navigator.cursor      = Cursor{.viewId = "a", .step = 0UZ};
        expect(navigator.graph.validate().has_value()) << "the branching graph must validate";

        expect(navigator.next());
        expect(eq(navigator.cursor.viewId, std::string{"b"})) << "next follows the first declared edge";
        expect(navigator.next());
        expect(eq(navigator.cursor.viewId, std::string{"d"})) << "both branches rejoin";

        expect(navigator.jumpTo("c")) << "the other branch is reachable by name";
        expect(eq(navigator.cursor.viewId, std::string{"c"}));
        expect(!navigator.jumpTo("nowhere")) << "a target that does not exist is refused";
    };

    "previous follows where the presenter actually came from, not the reverse edge"_test = [] {
        Navigator navigator;
        navigator.graph.views = {View{.id = "a", .stepCount = 1UZ, .next = {"b", "c"}}, View{.id = "b", .stepCount = 1UZ, .next = {}}, View{.id = "c", .stepCount = 1UZ, .next = {}}};
        navigator.cursor      = Cursor{.viewId = "a", .step = 0UZ};
        expect(navigator.jumpTo("c"));
        expect(navigator.previous());
        expect(eq(navigator.cursor.viewId, std::string{"a"})) << "history, not graph order";
    };

    "linear navigation needs no explicit edges"_test = [] {
        const NavigationGraph graph = linearGraph();
        expect(eq(graph.defaultNext("intro"), std::string{"architecture"}));
        expect(eq(graph.defaultNext("demo"), std::string{}));
    };

    "explicit edges take precedence over document order"_test = [] {
        NavigationGraph graph    = linearGraph();
        graph.views.front().next = {"demo", "architecture"};
        expect(eq(graph.defaultNext("intro"), std::string{"demo"}));
    };

    "next advances the reveal step before following an edge"_test = [] {
        Navigator navigator{.graph = linearGraph(), .cursor = Cursor{.viewId = "architecture", .step = 0UZ}, .history = {}};
        expect(navigator.next());
        expect(navigator.cursor == Cursor{.viewId = "architecture", .step = 1UZ});
        expect(navigator.next());
        expect(navigator.cursor == Cursor{.viewId = "architecture", .step = 2UZ});
        expect(navigator.next());
        expect(navigator.cursor == Cursor{.viewId = "demo", .step = 0UZ});
    };

    "where next would go is told without going there"_test = [] {
        const Navigator navigator{.graph = linearGraph(), .cursor = Cursor{.viewId = "architecture", .step = 2UZ}, .history = {}};
        expect(navigator.nextCursor() == Cursor{.viewId = "demo", .step = 0UZ}) << "the last step of a view leads to the next view";
        expect(navigator.cursor == Cursor{.viewId = "architecture", .step = 2UZ}) << "and the cursor stays";
        expect(Navigator{.graph = linearGraph(), .cursor = Cursor{.viewId = "architecture", .step = 0UZ}, .history = {}}.nextCursor() == Cursor{.viewId = "architecture", .step = 1UZ});
        expect(Navigator{.graph = linearGraph(), .cursor = Cursor{.viewId = "demo", .step = 0UZ}, .history = {}}.nextCursor() == Cursor{.viewId = "demo", .step = 0UZ}) << "at the end there is nowhere to go";
    };

    "next stops at the last step of the last view"_test = [] {
        Navigator navigator{.graph = linearGraph(), .cursor = Cursor{.viewId = "demo", .step = 0UZ}, .history = {}};
        expect(!navigator.next());
        expect(navigator.cursor == Cursor{.viewId = "demo", .step = 0UZ});
    };

    "previous walks the actual navigation history"_test = [] {
        Navigator navigator{.graph = linearGraph(), .cursor = Cursor{.viewId = "intro", .step = 0UZ}, .history = {}};
        expect(navigator.jumpTo("demo"));
        expect(navigator.cursor == Cursor{.viewId = "demo", .step = 0UZ});
        expect(navigator.previous());
        expect(navigator.cursor == Cursor{.viewId = "intro", .step = 0UZ});
        expect(!navigator.previous());
    };

    "previous returns to the reveal step it came from"_test = [] {
        Navigator navigator{.graph = linearGraph(), .cursor = Cursor{.viewId = "architecture", .step = 0UZ}, .history = {}};
        expect(navigator.next());
        expect(navigator.next());
        expect(navigator.next());
        expect(navigator.cursor == Cursor{.viewId = "demo", .step = 0UZ});
        expect(navigator.previous());
        expect(navigator.cursor == Cursor{.viewId = "architecture", .step = 2UZ});
    };

    "jumping to an unknown view leaves the cursor untouched"_test = [] {
        Navigator navigator{.graph = linearGraph(), .cursor = Cursor{.viewId = "intro", .step = 0UZ}, .history = {}};
        expect(!navigator.jumpTo("nowhere"));
        expect(navigator.cursor == Cursor{.viewId = "intro", .step = 0UZ});
        expect(navigator.history.empty());
    };

    "next from an unknown view does nothing"_test = [] {
        Navigator navigator{.graph = linearGraph(), .cursor = Cursor{.viewId = "nowhere", .step = 0UZ}, .history = {}};
        expect(!navigator.next());
    };

    "non-linear edges are followed"_test = [] {
        NavigationGraph graph{.views = {
                                  View{.id = "a", .stepCount = 1UZ, .next = {"c"}},
                                  View{.id = "b", .stepCount = 1UZ, .next = {}},
                                  View{.id = "c", .stepCount = 1UZ, .next = {"b"}},
                              }};
        expect(graph.validate().has_value());
        Navigator navigator{.graph = graph, .cursor = Cursor{.viewId = "a", .step = 0UZ}, .history = {}};
        expect(navigator.next());
        expect(eq(navigator.cursor.viewId, std::string{"c"}));
        expect(navigator.next());
        expect(eq(navigator.cursor.viewId, std::string{"b"}));
    };

    "validation rejects an empty presentation"_test = [] {
        const auto result = NavigationGraph{}.validate();
        expect(!result.has_value());
        expect(result.error() == NavigationError::noViews);
    };

    "validation rejects duplicate view ids"_test = [] {
        const NavigationGraph graph{.views = {View{.id = "a", .stepCount = 1UZ, .next = {}}, View{.id = "a", .stepCount = 1UZ, .next = {}}}};
        const auto            result = graph.validate();
        expect(!result.has_value());
        expect(result.error() == NavigationError::duplicateViewId);
    };

    "validation rejects a view without a stable id"_test = [] {
        const auto result = NavigationGraph{.views = {View{.id = "", .stepCount = 1UZ, .next = {}}}}.validate();
        expect(!result.has_value());
        expect(result.error() == NavigationError::emptyViewId);
    };

    "validation rejects a view with zero reveal steps"_test = [] {
        const auto result = NavigationGraph{.views = {View{.id = "a", .stepCount = 0UZ, .next = {}}}}.validate();
        expect(!result.has_value());
        expect(result.error() == NavigationError::emptyStepCount);
    };

    "validation rejects an edge to an unknown view"_test = [] {
        const auto result = NavigationGraph{.views = {View{.id = "a", .stepCount = 1UZ, .next = {"missing"}}}}.validate();
        expect(!result.has_value());
        expect(result.error() == NavigationError::unknownNavigationTarget);
    };

    "a valid linear presentation validates"_test = [] { expect(linearGraph().validate().has_value()); };
};

int main() { return 0; }
