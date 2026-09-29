#ifndef GR4_PRESENT_SCOPED_WINDOW_HPP
#define GR4_PRESENT_SCOPED_WINDOW_HPP

#include <utility> // c_resource.hpp uses std::forward without including it

#include <c_resource.hpp>

#include <imgui.h>

namespace gr::present {

namespace scoped {
inline bool        beginChild(const char* id, const ImVec2& size, ImGuiChildFlags childFlags, ImGuiWindowFlags windowFlags) { return ImGui::BeginChild(id, size, childFlags, windowFlags); }
inline void        popStyleColor() { ImGui::PopStyleColor(); }
inline void        popStyleVar() { ImGui::PopStyleVar(); }
inline ImDrawList* pushClipRect(ImDrawList* list, ImVec2 min, ImVec2 max) {
    if (list != nullptr) {
        list->PushClipRect(min, max, true);
    }
    return list;
}
inline void popClipRect(ImDrawList* list) { list->PopClipRect(); }
} // namespace scoped

/// what ImGui opens and closes in pairs, closed at the end of the scope; the shapes of OpenDigitizer's ImguiWrap.hpp, which also pulls in
/// imgui-node-editor; a trailing `true` closes even when opening returned false, as ImGui requires of `Begin` and `BeginChild`
using ScopedWindow     = stdex::c_resource<bool, ImGui::Begin, ImGui::End, true>;
using ScopedChild      = stdex::c_resource<bool, scoped::beginChild, ImGui::EndChild, true>;
using ScopedId         = stdex::c_resource<void, static_cast<void (*)(const char*)>(ImGui::PushID), ImGui::PopID, true>;
using ScopedIndexId    = stdex::c_resource<void, static_cast<void (*)(int)>(ImGui::PushID), ImGui::PopID, true>;
using ScopedPointerId  = stdex::c_resource<void, static_cast<void (*)(const void*)>(ImGui::PushID), ImGui::PopID, true>;
using ScopedStyleColor = stdex::c_resource<void, static_cast<void (*)(ImGuiCol, ImU32)>(ImGui::PushStyleColor), scoped::popStyleColor, true>;
using ScopedStyleVar   = stdex::c_resource<void, static_cast<void (*)(ImGuiStyleVar, const ImVec2&)>(ImGui::PushStyleVar), scoped::popStyleVar, true>;
using ScopedStyleValue = stdex::c_resource<void, static_cast<void (*)(ImGuiStyleVar, float)>(ImGui::PushStyleVar), scoped::popStyleVar, true>;
/// a draw list's clip rectangle, intersected with the one it had; a null list clips nothing, for a clip that only some cases need
using ScopedClipRect = stdex::c_resource<ImDrawList*, scoped::pushClipRect, scoped::popClipRect>;

} // namespace gr::present

#endif // GR4_PRESENT_SCOPED_WINDOW_HPP
