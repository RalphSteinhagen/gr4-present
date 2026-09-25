#ifndef GR4_PRESENT_IMGUI_SCOPED_HPP
#define GR4_PRESENT_IMGUI_SCOPED_HPP

#include <utility> // c_resource.hpp uses std::forward without including it

#include <c_resource.hpp>

#include <imgui.h>

namespace gr::present {

/**
 * ImGui::End() must follow every ImGui::Begin(), including when Begin() returns false, which is what the trailing
 * `true` encodes. OpenDigitizer's ImguiWrap.hpp declares the same aliases but also pulls in imgui-node-editor.
 */
using ScopedWindow = stdex::c_resource<bool, ImGui::Begin, ImGui::End, true>;

} // namespace gr::present

#endif // GR4_PRESENT_IMGUI_SCOPED_HPP
