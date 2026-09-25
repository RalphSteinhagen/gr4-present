#include "Fonts.hpp"

#include "EmbeddedLogos.hpp"

namespace gr::present {

Fonts& Fonts::instance() {
    static Fonts fonts;
    return fonts;
}

void Fonts::load() {
    ImFontConfig configuration;
    configuration.FontDataOwnedByAtlas = false; // the bytes are embedded in the binary and outlive the atlas
    face                               = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kRobotoMediumTtf.data()), static_cast<int>(kRobotoMediumTtf.size()), 0.0f, &configuration);
}

} // namespace gr::present
