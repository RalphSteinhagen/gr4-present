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

/**
 * What a box's fence asked for: `:::left {shrink=off}`.
 *
 * The settings a box can carry rather than a slide, which is why they sit next to the content they govern. A flag
 * written on its own is on; `off`, `false` and `no` turn one back off, so an author who writes what they mean is
 * understood either way round.
 */
inline constexpr float kRevealSeconds = 0.4f; // how long a CPU reveal takes when the deck does not say

struct BoxOptions {
    bool shrink  = true;  // content that does not fit is scaled down, until it is told not to and clipped instead
    bool centre  = false; // content sits at the top of its box, unless the author asks for the middle
    bool striped = false; // alternate rows of a table in this box carry a light wash, under a stronger one on the header
    bool notes   = true;  // the notes a box cites are listed at its foot; off leaves them to the References view
    bool bottom  = false; // content sits at the foot of its box, which is where the last of a column's boxes belongs
    bool frame   = false; // `{frame}`: the box is drawn, opaque and bordered, so where it stops shows and it covers what it overlaps
    bool left    = false; // `{left}`: a displayed formula sits at the box's left edge rather than centred, to line up with prose
    bool fit     = false; // `{fit}`: what does not fit even at the floor is drawn smaller still rather than cut off

    bool slideNotes = false; // `notes=all`: the box lists the notes every box of the slide cites, for captions that carry only a marker

    std::optional<TypeSize> size = {}; // `size=14pt|80%`: the body size of this box; absent keeps the slide's
    std::string             font = {}; // `font=hand`: a role or a face the deck names; empty keeps the slide's

    std::string          in           = {};   // `in=fade|rise|wipe|grow|<effect>`: how the box arrives with its slide; empty is at once
    std::optional<float> inSeconds    = {};   // `dur=`; absent is as long as its way of arriving takes
    float                afterSeconds = 0.0f; // `after=`: how long after the slide's transition ends the box arrives
    std::string          inFrom       = {};   // `from=`: where it rises from, or its wipe starts
    std::string          overlay      = {};   // `overlay=<effect>` and its `overlay.k=v` settings: an effect drawn from the box

    bool operator==(const BoxOptions&) const = default;
};

/// the options a `:::<name>` fence carries, read once so the rest of the viewer never parses a fence again
[[nodiscard]] BoxOptions boxOptionsOf(const Block& block) noexcept;

/// the prose a `:::<name>` block put in the box of that name, with what its fence asked for
struct AreaDocument {
    std::string id;
    Document    contents;
    BoxOptions  options;
};

/**
 * Draws a parsed presentation document, honouring the reveal step.
 *
 * Layout is a single column whose width is a fraction of the viewport, because a line of text wider than roughly
 * ninety characters is unreadable from the back of a room. Sizes derive from the viewport height for the same
 * reason `Fonts` does: a presentation is shown on everything from a phone to a projector.
 *
 * Images resolve through a caller-supplied lookup rather than being loaded here: this component knows how to draw a
 * document, not where a package lives. A reference that does not resolve draws a labelled placeholder, because a
 * silently missing figure is worse on a projector than a visible gap.
 */
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

    /**
     * Draws a live region's widget into the box the layout gave it, and says whether anything was drawn.
     *
     * A lookup rather than something this view knows how to do: what fills the box is a block of a running GNU
     * Radio graph, and a view whose job is to draw a document should not be able to name a scheduler. One that
     * draws nothing leaves the labelled outline, which is what a viewer built without the runtime shows.
     */
    struct RegionBoxes {
        Rectangle                charts;  // what is left of the region for its charts
        std::optional<Rectangle> toolbar; // `toolbar:` asked for: a strip above the charts, or the area it names
        std::optional<Rectangle> status;  // `status:` asked for: a strip below the charts, or the area it names
    };
    using RegionLookup = std::function<bool(std::string_view, const RegionBoxes&)>;

    /// the links a drawing carries, in fractions of it, so a click on what they wrap follows them
    using LinkLookup = std::function<std::span<const SvgLink>(std::string_view)>;

    Document document = {};
    int      step     = 0; // blocks above this reveal step stay hidden

    /// seconds since the step just reached began to arrive, its blocks each as their step or `with` says; negative
    /// is at once: stepping back, a deep link, a reload
    float stepSeconds = -1.0f;
    /// whether the slide is being entered going forward, so its boxes with `in=` arrive, and how long ago its
    /// transition ended -- negative while it still runs, which holds a box back rather than showing it
    bool          arriving       = false;
    float         arrivedSeconds = -1.0f;
    ImageLookup   imageFor       = {};
    DataLookup    dataFor        = {};
    FormulaLookup formulaFor     = {};
    QrLookup      qrFor          = {};
    VideoLookup   videoFor       = {};
    RegionLookup  regionFor      = {};
    const char*   windowName     = "##document"; // the ImGui window it draws into; a miniature draws into one of its own
    LinkLookup    linksFor       = {};

    /// where the deck was opened from, in a browser; the references' codes encode it rather than the address the
    /// deck names, which is only where it is published. Empty natively, where the deck has no address of its own.
    std::string deckAddress = {};

    /// every link in the whole deck, label and target, for a `:::references` directive to list
    std::vector<std::pair<std::string, std::string>> references = {};

    /// every citation label in the whole deck, in the order it is first referred to. IEEE numbers a work by where
    /// it first appears in the document and uses that number everywhere, so this decides both the marker on a
    /// slide and the position in the reference list. Empty falls back to numbering within the view, which is what
    /// a view drawn on its own can know.
    std::vector<std::string> citations = {};

    /**
     * An optional SVG master layout. When present the backdrop is drawn to fill the view and content goes into the
     * areas the author named: the section heading into `title`, everything else into `content`, and a live region
     * into the area its `region:` field names. Without one the document is laid out in a centred reading column.
     */
    /// while a page is exported: everything this view draws, in the terms a PDF page needs; null otherwise
    PageRecording* recording = nullptr;

    const Layout*            layout         = nullptr;
    const Texture*           layoutBackdrop = nullptr;
    std::string              layoutSource   = {}; // the package reference `layoutBackdrop` was drawn from
    std::vector<std::string> layoutHidden   = {}; // the ids of its elements it was drawn without
    /// the three parts of the footer: who and what at the left, which slide at the right
    std::string footerText   = {}; // the talk's title and its author
    std::string footerNumber = {}; // "7" or "7 / 41", empty when the deck asks for no numbering

    /// area id -> package-relative resource, from the layout directive's unreserved keys
    std::vector<std::pair<std::string, std::string>> areaContent = {};

    /// the prose a `:::<name>` block put in each box, already parsed
    std::vector<AreaDocument> areaDocuments = {};

    /**
     * A photograph or illustration the camera pans and zooms over, and the part of it currently in shot.
     *
     * `cameraUv` is in fractions of the picture, which is also what a texture's coordinates are, so the frame is
     * the texture rectangle to sample and nothing has to know the file's pixel size. An empty rectangle is the
     * whole picture. `cameraScope` decides whether the title and footer stay put or travel with the picture.
     */
    const Texture* cameraImage  = nullptr;
    std::string    cameraSource = {}; // the package reference `cameraImage` was drawn from
    Rectangle      cameraUv     = {};
    CameraScope    cameraScope  = CameraScope::image;

    /// a move between two stops of the picture in progress; the camera frames its `progress` instead of `cameraUv`
    struct CameraMove {
        Rectangle from;
        Rectangle via; ///< empty for the straight move
        Rectangle to;
        float     progress = 0.0f; ///< 0 to 1, already eased

        [[nodiscard]] bool active() const noexcept { return to.width > 0.0f && to.height > 0.0f; }
    };
    CameraMove cameraMove = {};

    /// degrees the camera is turned by, as in Sozi: the scenery -- a drawing under a caption, or a photograph under the
    /// image scope -- turns the other way about the middle of its room; the words stay upright
    float cameraRotation = 0.0f;

    /// what a camera stop says about the region it frames, drawn beside it over the picture
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

    /**
     * The part of the layout the camera currently frames, in the layout's own units. A zero-width frame means the
     * whole document, which is what a section without an anchor gets. Interpolating this between two sections'
     * anchors is the Sozi-style spatial move.
     */
    Rectangle cameraFrame = {};

    /// 0 draws nothing, 1 draws opaque; a cross-fade runs two views at complementary values
    float opacity = 1.0f;

    /// region ids encountered while drawing, with the rectangle each occupies, for the live-region binder
    struct PlacedRegion {
        std::string id;
        ImVec2      min;
        ImVec2      max;
    };
    std::vector<PlacedRegion> placedRegions = {};

    /// what a button under a clip does; the viewer carries it out, because this view draws and does not own the clips
    enum class VideoAction { playPause, rewind, muteToggle };

    /// a button drawn under a clip, in the slide's own coordinates, for whoever turns a click into an action
    struct VideoButton {
        std::string source;
        VideoAction action = VideoAction::playPause;
        Rectangle   area;
        bool        active = false; // playing, for play and pause; silenced, for mute
    };
    std::vector<VideoButton> videoButtons = {}; // refreshed each draw

    /// words or a code that lead somewhere, in the slide's own coordinates, for whoever turns a click into opening it
    struct LinkArea {
        std::string target;
        Rectangle   area;
    };
    std::vector<LinkArea> linkAreas = {}; // refreshed each draw

    /// the text this view drew in the last frame, in reading order, for a reader to select and copy; `mutable`
    /// because the backdrop is drawn from a const placement pass and its text is selectable too
    mutable TextRuns textRuns  = {};
    ImDrawList*      drawnInto = nullptr; // what the last draw put the slide into, for whoever magnifies it

    /// called with the slide's draw list before anything of the slide is in it, and after all of it is: where a
    /// transition that needs the slide as a picture puts the commands that capture it
    std::function<void(ImDrawList&)> beforeDrawing = {};
    std::function<void(ImDrawList&)> afterDrawing  = {};
    std::function<void(ImDrawList&)> afterBackdrop = {}; // after the master and before the words: a background effect
    /// draws what `list` holds from `firstCommand` on through the effect `effect`, a reveal `progress` (eased) of the
    /// way or an overlay at 1, `low`-`high` on screen; `key` names the box or block; false when it cannot, and a reveal
    /// falls back to the CPU fade
    /// how long a reveal `kind` lasts when the deck says nothing: an effect's `@duration`, else the CPU reveals' pace
    std::function<float(std::string_view kind)> revealSeconds = {};
    using ThroughEffect                                       = std::function<bool(ImDrawList& list, int firstCommand, std::string_view effect, std::string_view key, float progress, ImVec2 low, ImVec2 high)>;
    ThroughEffect throughEffect                               = {};

    /// each block the last draw painted, and each line of its code blocks, by what identifies it across slides -- its
    /// `{#id}`, else its kind and words -- and the vertices it drew, so a morph can move it to where the next slide has it
    std::vector<DrawnPiece> drawnBlocks = {};

    /// a box whose content did not fit, with the numbers an author needs to decide what to do about it
    struct Overrun {
        std::string id;
        float       wanted    = 0.0f;  // the height the content asked for, at the scale it was drawn
        float       available = 0.0f;  // the height the box gave it
        bool        shrank    = false; // whether it was scaled down first, or fixed by the author and simply cut
    };

    /// the notes the boxes of this slide cite, in order, for a box that lists them all; refreshed each draw
    std::vector<std::string> slideNotes = {};

    /// boxes whose content did not fit, whether cut off or scaled to the floor and still too tall; refreshed each draw
    std::vector<Overrun> clipped = {};

    /// "region/area" for each toolbar or status bar whose area the layout does not have; refreshed each draw
    mutable std::vector<std::string> barsWithoutArea = {};

    /// content taller than the view is scaled down uniformly to this floor, 12 pt on an 18 pt body, and clipped beyond
    /// it; the deck's `sizes:` move it, and a size the author set has its own (`Placement::floorScale`)
    float minimumScale = TypeScale{}.floor / TypeScale{}.body;

    std::string slideFont = {}; // `font:` of the slide's `:::layout`

    bool                    outlineAreas = false; // `outline: on` of the slide's `:::layout`: its areas drawn, named
    std::optional<TypeSize> slideSize    = {};    // `size:` of the slide's `:::layout`, its body size
    /// the scale the last draw used; 1 when everything fitted
    float appliedScale = 1.0f;
    float trailingGap  = 0.0f; // the space the last pass left after its last block, which is not part of what a box needs

    void draw(const Theme& theme);

    /**
     * The height `contents` needs at `width`, so a grid can size a row to the words in it.
     *
     * A row's width does not depend on any row's height, so a caller lays the grid out once to learn the widths,
     * asks this what each row then wants, and lays it out again. `room` bounds what a plot inside may claim.
     */
    [[nodiscard]] float heightOf(const Theme& theme, const Document& contents, float width, float bodyPixels, float room, const BoxOptions& options = {});

    /**
     * The reading column, in pixels: the window less a margin on each side.
     *
     * The margin is three ems, because a margin is a property of the type and not of the canvas, but never more
     * than a small share of a narrow window. There is no cap on the measure: a line runs the width it is given.
     * A cap of about forty-six ems was here, and the deck's author asked for the width instead; if it comes back
     * it belongs on the box that wants it rather than on every slide.
     */
    [[nodiscard]] static float columnWidth(float viewportWidth, float bodyPixels) noexcept;

    /// where a pass draws: the body column, and the title and footer boxes when the layout names them
    struct Placement {
        float contentLeft  = 0.0f;
        float contentTop   = 0.0f;
        float contentWidth = 0.0f;
        float available    = 0.0f; // height the content may occupy before it must be scaled
        float bodyPixels   = 0.0f; // the body text size this slide is set at, which everything else is a multiple of
        float titlePixels  = 0.0f; // the body size the title is a multiple of; zero takes bodyPixels, a drawn master sets it from the deck

        Rectangle title;            // zero-width when the layout names no title area
        Rectangle footer;           // likewise
        Rectangle slideNumber = {}; // a master's `slide_number` box: the number goes there, centred, not into the footer

        BoxOptions options;      // what the fence of the box being drawn asked for
        float      inset = 0.6f; // body heights of air above and below the words; a box inside a grid has its gutter for that

        // how the master maps onto the view, so any other named area can be placed without laying out again
        float factor  = 1.0f;
        float originX = 0.0f;
        float originY = 0.0f;

        // a split master: its top band down to `splitTop` and its bottom band from `splitBottom` keep their shape at the
        // screen's width, and the band between stretches to the height left; zero `splitHeight` is a plain master
        float splitTop     = 0.0f;
        float splitBottom  = 0.0f;
        float splitHeight  = 0.0f;
        float middleFactor = 0.0f;
        float screenBottom = 0.0f;

        float floorScale = 0.0f; // the smallest scale auto-shrink may reach; zero is the deck's floor over its body

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
    enum class Selection { all, headingOnly, exceptHeading, notesOnly }; ///< `notesOnly`: the body's notes, pinned to the foot, and nothing else

    /// lays the selected blocks out at `scale` within `where`, painting only when asked; returns the height used
    float layoutPass(const Theme& theme, float scale, bool paint, const Placement& where, Selection selection);
    void  drawAreaOutlines(const Theme& theme, const Placement& where) const;

    struct Pass;
    [[nodiscard]] std::vector<std::string> citedLabels(Selection selection, const Placement& where) const;
    void                                   layoutVideo(Pass& pass, const Block& block);
    void                                   layoutQr(Pass& pass, const Block& block);
    void                                   layoutReferences(Pass& pass, const Block& block);
    void                                   layoutRegion(Pass& pass, const Block& block);
    void                                   layoutFootnotes(Pass& pass, std::span<const std::string> referenced);

    /// the largest scale, no smaller than `minimumScale`, at which `where` holds what `wanted` says it needs at full size
    float fittingScale(const Theme& theme, const Placement& where, Selection selection, float wanted);

    static constexpr float kFitTolerance = 0.5f; // pixels

    /// draws the backdrop and returns where the body, heading and footer go
    [[nodiscard]] Placement bodyPlacement(bool paint) const;

    /// draws `cameraImage` framed by `cameraUv`; returns the rectangle it covered, empty when there is no picture
    /// where the picture lands: `box` is the room it was given, `drawn` the rectangle it covers in it after being
    /// fitted, and `uv` the part of the texture that fills `drawn`
    struct CameraGeometry {
        Rectangle box;
        Rectangle drawn;
        Rectangle uv;    ///< the part of the texture that is on screen, so never outside the picture
        Rectangle frame; ///< the frame the camera was aimed at, which during a move may run past the picture's edge

        /// where `part`, in fractions of the picture, lands on screen
        [[nodiscard]] Rectangle onScreen(const Rectangle& part) const noexcept;
    };

    /// the geometry a picture framed by `wantedUv` would take, without drawing it
    [[nodiscard]] CameraGeometry cameraGeometry(float reservedTop, const Rectangle& wantedUv, const CameraMove* move = nullptr, float reservedBottom = 0.0f) const;

    void drawInfoBox(const Theme& theme, const Rectangle& face, const Rectangle& screen);

    [[nodiscard]] Rectangle drawCameraImage(float opacityNow, const CameraGeometry& geometry, const Rectangle& clip) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_DOCUMENT_VIEW_HPP
