#ifndef GR4_PRESENT_DOCUMENT_VIEW_HPP
#define GR4_PRESENT_DOCUMENT_VIEW_HPP

#include "Fonts.hpp"
#include "FormulaCache.hpp"
#include "Morph.hpp"
#include "QrCode.hpp"
#include "RasterCamera.hpp"
#include "SvgImage.hpp"
#include "TextRuns.hpp"
#include "Texture.hpp"
#include "Theme.hpp"
#include "VideoCache.hpp"

#include <gr4-present/Highlight.hpp>
#include <gr4-present/Markdown.hpp>
#include <gr4-present/RegionGeometry.hpp>
#include <gr4-present/export/PageRecording.hpp>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

/// a table's column widths in `available` pixels: as asked when they fit, else a column within its fair share
/// (available / columns) keeps its ask and the others divide what is left in proportion to theirs
[[nodiscard]] std::vector<float> columnWidthsSharing(std::span<const float> wanted, float available);

/// the largest scale in [floor, 1] at which `measure` says the content fits, and what it laid out there: from `estimate`,
/// six halvings of what is left; `measure(scale)` returns {fits, laid out}, fitting may not get worse as the scale falls,
/// and a content that does not fit even at `floor` gets `floor`
template<typename TLaidOut, typename TMeasure>
[[nodiscard]] std::pair<float, TLaidOut> largestFitting(float floor, float estimate, TMeasure measure) {
    float low                  = floor;
    float high                 = 1.0f;
    auto [fitsEstimate, atLow] = measure(estimate);
    if (fitsEstimate) {
        low = estimate;
    } else {
        high                            = estimate;
        const auto [fitsFloor, atFloor] = measure(floor);
        atLow                           = atFloor;
        if (!fitsFloor) {
            return {floor, atLow};
        }
    }
    for (int refinement = 0; refinement < 6; ++refinement) {
        const float middle = 0.5f * (low + high);
        if (auto [fitsMiddle, atMiddle] = measure(middle); fitsMiddle) {
            low   = middle;
            atLow = atMiddle;
        } else {
            high = middle;
        }
    }
    return {low, atLow};
}

inline constexpr float kRevealSeconds = 0.4f; // CPU reveal duration when the deck does not say

/// what a box's fence asked for, e.g. `:::left {shrink=off}`; a flag alone is on, `off`, `false` and `no` turn it off
struct BoxOptions {
    bool shrink  = true;  // scales content that does not fit down; off clips instead
    bool centre  = false; // vertically centred rather than at the top
    bool striped = false; // light wash on alternate table rows, stronger on the header
    bool notes   = true;  // lists cited notes at the box's foot; off leaves them to the References view
    bool bottom  = false; // content at the foot of its box
    bool frame   = false; // `{frame}`: drawn opaque and bordered, covering what it overlaps
    bool left    = false; // `{left}`: displayed formulas at the left edge rather than centred
    bool fit     = false; // `{fit}`: content too big even at the floor is drawn smaller still, not cut off

    bool slideNotes = false; // `notes=all`: lists the notes every box of the slide cites

    std::optional<TypeSize> size = {}; // `size=14pt|80%`: the body size of this box; absent keeps the slide's
    std::string             font = {}; // `font=hand`: a role or a face the deck names; empty keeps the slide's

    std::string          in           = {};   // `in=fade|rise|wipe|grow|<effect>`: how the box arrives with its slide; empty is at once
    std::optional<float> inSeconds    = {};   // `dur=`; absent takes as long as the arrival does
    float                afterSeconds = 0.0f; // `after=`: delay after the slide's transition ends
    std::string          inFrom       = {};   // `from=`: where it rises from or its wipe starts
    std::string          overlay      = {};   // `overlay=<effect>` and its `overlay.k=v` settings

    bool operator==(const BoxOptions&) const = default;
};

/// the options a `:::<name>` fence carries
[[nodiscard]] BoxOptions boxOptionsOf(const Block& block) noexcept;

/// the prose a `:::<name>` block put in the box of that name, with what its fence asked for
struct AreaDocument {
    std::string id;
    Document    contents;
    BoxOptions  options;
};

/// draws a parsed document at its reveal step, in a centred reading column or the areas of a master layout; images,
/// data, formulas, videos and live regions resolve through caller-supplied lookups; a miss draws a labelled placeholder
struct DocumentView {
    /// resolves an image reference to a texture the caller keeps alive; nullptr draws a placeholder instead
    using ImageLookup = std::function<const Texture*(std::string_view)>;

    /// resolves a package-relative data file, such as a plot's CSV, to its text; empty when it cannot be read
    using DataLookup = std::function<std::string_view(std::string_view)>;

    /// rasterises a formula at a text size; the bitmap is white, for the caller to tint
    using FormulaLookup = std::function<const PlacedFormula*(std::string_view, float, bool)>;

    /// encodes a URL as a QR code, drawn dark on light whatever the scheme is
    using QrLookup = std::function<const PlacedQrCode*(std::string_view, float)>;

    /// the frame of a clip that should be showing now, with how far through it is
    using VideoLookup = std::function<const PlayingVideo*(std::string_view, bool, bool, bool)>;

    /// the rectangles of a live region
    struct RegionBoxes {
        Rectangle                charts;
        std::optional<Rectangle> toolbar; // `toolbar:` asked for: a strip above the charts, or the area it names
        std::optional<Rectangle> status;  // `status:` asked for: a strip below the charts, or the area it names
    };

    /// draws a live region's widget into its box and says whether anything was drawn; if not, the outline stays
    using RegionLookup = std::function<bool(std::string_view, const RegionBoxes&)>;

    /// the links a drawing carries, in fractions of it, so a click on what they wrap follows them
    using LinkLookup = std::function<std::span<const SvgLink>(std::string_view)>;

    const Document* document = nullptr; // the slide drawn; set before every draw and owned by the presentation's sections
    int             step     = 0;       // blocks above this reveal step stay hidden

    /// seconds since the current step began to arrive; negative is at once (stepping back, deep link, reload)
    float stepSeconds = -1.0f;
    /// whether the slide is entered going forward, so boxes with `in=` arrive, and seconds since its transition ended
    /// (negative while it runs, which holds a box back)
    bool          arriving       = false;
    float         arrivedSeconds = -1.0f;
    ImageLookup   imageFor       = {};
    DataLookup    dataFor        = {};
    FormulaLookup formulaFor     = {};
    QrLookup      qrFor          = {};
    VideoLookup   videoFor       = {};
    RegionLookup  regionFor      = {};
    const char*   windowName     = "##document"; // the ImGui window it draws into
    LinkLookup    linksFor       = {};

    /// where the deck was opened from, in a browser; the references' codes encode it; empty natively
    std::string deckAddress = {};

    /// every link in the deck, label and target, for `:::references`
    std::vector<std::pair<std::string, std::string>> references = {};

    /// every citation label in the deck in order of first reference (IEEE numbering); empty numbers within the view
    std::vector<std::string> citations = {};

    /// while a page is exported: everything this view draws, in the terms a PDF page needs; null otherwise
    PageRecording* recording = nullptr;

    /// optional SVG master layout: the backdrop fills the view, the heading goes into `title`, other content into
    /// `content`, a live region into the area its `region:` names; without it the document is a centred reading column
    const Layout*            layout         = nullptr;
    const Texture*           layoutBackdrop = nullptr;
    std::string              layoutSource   = {}; // package reference `layoutBackdrop` was drawn from
    std::vector<std::string> layoutHidden   = {}; // ids of the elements it was drawn without
    std::string              footerText     = {}; // the talk's title and author
    std::string              footerNumber   = {}; // "7" or "7 / 41", empty when the deck asks for no numbering

    /// area id -> package-relative resource, from the layout directive's unreserved keys
    std::vector<std::pair<std::string, std::string>> areaContent = {};

    std::vector<AreaDocument> areaDocuments = {};

    /// a photograph or illustration the camera pans and zooms over; `cameraUv` is the shot in picture fractions (empty
    /// is the whole picture) and `cameraScope` decides whether title and footer stay put or travel with it
    const Texture* cameraImage  = nullptr;
    std::string    cameraSource = {}; // package reference `cameraImage` was drawn from
    Rectangle      cameraUv     = {};
    CameraScope    cameraScope  = CameraScope::image;

    /// a move between two stops in progress; the camera frames its `progress` instead of `cameraUv`
    struct CameraMove {
        Rectangle from;
        Rectangle via; ///< empty for the straight move
        Rectangle to;
        float     progress = 0.0f; ///< 0 to 1, already eased

        [[nodiscard]] bool active() const noexcept { return to.width > 0.0f && to.height > 0.0f; }
    };
    CameraMove cameraMove = {};

    /// degrees, as in Sozi: the scenery turns the other way about the middle of its room; the words stay upright
    float cameraRotation = 0.0f;

    /// what a camera stop says about the region it frames, drawn beside it
    struct InfoBox {
        Document    contents;
        Document    aside = {};     ///< a stop's `:::aside`: a second box, on the other side of the region
        Rectangle   region;         ///< in fractions of the picture
        std::string side;           ///< `left`, `right`, `above`, `below`, or empty for the side with more room
        float       opacity = 0.0f; ///< 0 hides it; it fades in once the camera has stopped
    };
    InfoBox   infoBox          = {};
    Rectangle infoBoxDrawn     = {}; ///< where the box was drawn last frame, empty when it was not
    Rectangle infoFaceDrawn    = {}; ///< where the region it describes was on screen last frame
    Rectangle cameraFrameDrawn = {}; ///< the part of the picture on screen last frame, in fractions

    /// the part of the layout the camera frames, in the layout's own units; zero width is the whole document
    Rectangle cameraFrame = {};

    /// 0 draws nothing, 1 draws opaque; a cross-fade runs two views at complementary values
    float opacity = 1.0f;

    /// region ids met while drawing, with the rectangle each occupies, for the live-region binder
    struct PlacedRegion {
        std::string id;
        ImVec2      min;
        ImVec2      max;
    };
    std::vector<PlacedRegion> placedRegions = {};

    /// what a button under a clip does; the viewer carries it out
    enum class VideoAction { playPause, rewind, muteToggle };

    /// a button drawn under a clip, in the slide's coordinates
    struct VideoButton {
        std::string source;
        VideoAction action = VideoAction::playPause;
        Rectangle   area;
        bool        active = false; // playing, for play and pause; silenced, for mute
    };
    std::vector<VideoButton> videoButtons = {}; // refreshed each draw

    /// words or a code that lead somewhere, in the slide's coordinates
    struct LinkArea {
        std::string target;
        Rectangle   area;
    };
    std::vector<LinkArea> linkAreas = {}; // refreshed each draw

    /// the text drawn in the last frame, in reading order; `mutable` because the backdrop is drawn from a const
    /// placement pass
    mutable TextRuns textRuns  = {};
    ImDrawList*      drawnInto = nullptr; // what the last draw put the slide into

    /// called with the slide's draw list before and after the slide: where a capturing transition puts its commands
    std::function<void(ImDrawList&)> beforeDrawing = {};
    std::function<void(ImDrawList&)> afterDrawing  = {};
    std::function<void(ImDrawList&)> afterBackdrop = {}; // after the master and before the words: a background effect
    /// how long a reveal `kind` lasts when the deck says nothing: an effect's `@duration`, else the CPU reveals' pace
    std::function<float(std::string_view kind)> revealSeconds = {};
    /// draws what `list` holds from `firstCommand` on through `effect`, a reveal `progress` (eased) of the way or an
    /// overlay at 1, `low`-`high` on screen; `key` names the box or block; false when it cannot (a reveal then fades)
    using ThroughEffect         = std::function<bool(ImDrawList& list, int firstCommand, std::string_view effect, std::string_view key, float progress, ImVec2 low, ImVec2 high)>;
    ThroughEffect throughEffect = {};

    /// each block and code line the last draw painted, keyed by `{#id}` else kind and words, with its vertices
    std::vector<DrawnPiece> drawnBlocks = {};

    /// a box whose content did not fit
    struct Overrun {
        std::string id;
        float       wanted    = 0.0f;  // height the content asked for, at the scale drawn
        float       available = 0.0f;  // height the box gave it
        bool        shrank    = false; // scaled down first; otherwise fixed by the author and cut
    };

    /// the notes the boxes of this slide cite, in order; refreshed each draw
    std::vector<std::string> slideNotes = {};

    /// boxes whose content did not fit, cut off or still too tall at the floor; refreshed each draw
    std::vector<Overrun> clipped = {};

    /// "region/area" for each toolbar or status bar whose area the layout does not have; refreshed each draw
    mutable std::vector<std::string> barsWithoutArea = {};

    std::string slideFont = {}; // `font:` of the slide's `:::layout`

    bool                    outlineAreas = false; // `outline: on` of the slide's `:::layout`
    std::optional<TypeSize> slideSize    = {};    // `size:` of the slide's `:::layout`
    /// the scale the last draw used; 1 when everything fitted
    float appliedScale = 1.0f;

    void draw(const Theme& theme);

    /// the height `contents` needs at `width`, so a grid can size a row; `room` bounds what a plot may claim
    [[nodiscard]] float heightOf(const Theme& theme, const Document& contents, float width, float bodyPixels, float room, const BoxOptions& options = {});

    /// the reading column in pixels: the window less a margin of three ems each side, capped for a narrow window
    [[nodiscard]] static float columnWidth(float viewportWidth, float bodyPixels) noexcept;

    /// where a pass draws: the body column, and the title and footer boxes when the layout names them
    struct Placement {
        float contentLeft  = 0.0f;
        float contentTop   = 0.0f;
        float contentWidth = 0.0f;
        float available    = 0.0f; // height the content may occupy before it is scaled
        float bodyPixels   = 0.0f; // body text size of this slide; everything else is a multiple
        float titlePixels  = 0.0f; // the title is a multiple of this; zero takes bodyPixels, a drawn master sets it from the deck

        Rectangle title       = {}; // zero-width when the layout names no title area
        Rectangle footer      = {}; // likewise
        Rectangle slideNumber = {}; // a master's `slide_number` box: the number goes there, centred, not into the footer

        BoxOptions options = {};   // fence options of the box being drawn
        float      inset   = 0.6f; // body heights of air above and below the words

        // maps the master onto the view
        float factor  = 1.0f;
        float originX = 0.0f;
        float originY = 0.0f;

        // split master: bands above `splitTop` and below `splitBottom` keep their shape, the middle stretches; zero
        // `splitHeight` is a plain master
        float splitTop     = 0.0f;
        float splitBottom  = 0.0f;
        float splitHeight  = 0.0f;
        float middleFactor = 0.0f;
        float screenBottom = 0.0f;

        float floorScale = 0.0f; // smallest scale auto-shrink may reach; zero is the deck's floor over its body

        [[nodiscard]] float mapY(float y) const noexcept {
            if (splitHeight <= 0.0f) {
                return originY + y * factor;
            }
            return y <= splitTop ? originY + y * factor : y >= splitBottom ? screenBottom - (splitHeight - y) * factor : originY + splitTop * factor + (y - splitTop) * middleFactor;
        }
        [[nodiscard]] Rectangle map(const Area& area) const noexcept {
            const float top = mapY(area.y);
            return Rectangle{.x = originX + area.x * factor, .y = top, .width = area.width * factor, .height = mapY(area.y + area.height) - top};
        }
    };

private:
    /// which blocks a pass draws: a layout with a `title` area takes the heading out of the flow
    enum class Selection { all, headingOnly, exceptHeading, notesOnly }; ///< `notesOnly`: the body's notes pinned to the foot

    /// what a pass measured: the height used, of which `trailingGap` is the space after the last block, not part of what a box needs
    struct LaidOut {
        float height      = 0.0f;
        float trailingGap = 0.0f;

        [[nodiscard]] float heightToFit() const noexcept { return height - trailingGap; }
    };

    /// lays the selected blocks of `contents` out at `scale` within `where`, painting only when asked
    LaidOut layoutPass(const Theme& theme, const Document& contents, float scale, bool paint, const Placement& where, Selection selection);
    void    drawAreaOutlines(const Theme& theme, const Placement& where) const;

    struct Pass;
    [[nodiscard]] std::vector<std::string> citedLabels(const Document& contents, Selection selection, const Placement& where) const;
    void                                   layoutVideo(Pass& pass, const Block& block);
    void                                   layoutQr(Pass& pass, const Block& block);
    void                                   layoutReferences(Pass& pass, const Block& block);
    void                                   layoutRegion(Pass& pass, const Block& block);
    void                                   layoutFootnotes(Pass& pass, std::span<const std::string> referenced);

    /// the largest scale at which `where` holds `contents`, which measured `wanted` at full size, and what it laid out there;
    /// at least the deck's floor over its body size (`sizes:`), or `Placement::floorScale`
    std::pair<float, LaidOut> fittingScale(const Theme& theme, const Document& contents, const Placement& where, Selection selection, LaidOut wanted);

    static constexpr float kFitTolerancePixels = 0.5f; // pixels

    /// draws the backdrop and returns where the body, heading and footer go
    [[nodiscard]] Placement bodyPlacement(bool paint) const;

    /// the geometry a picture framed by `wantedUv` would take, without drawing it
    [[nodiscard]] CameraGeometry cameraGeometry(float reservedTop, const Rectangle& wantedUv, const CameraMove* move = nullptr, float reservedBottom = 0.0f) const;

    void drawInfoBox(const Theme& theme, const Rectangle& face, const Rectangle& screen);

    /// draws `cameraImage` framed by `cameraUv`; returns the rectangle it covered, empty when there is no picture
    [[nodiscard]] Rectangle drawCameraImage(float opacityNow, const CameraGeometry& geometry, const Rectangle& clip) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_DOCUMENT_VIEW_HPP
