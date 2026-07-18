#include "gui/AssetsPane.h"
#include "api/StateAPI.h"
#include <set>
#include <cmath>

AssetsPane::AssetsPane(StateAPI& stateRef) : state(stateRef) {
    stateSubId = state.events().subscribe([this](const StateEvent& e) {
        // Audio membership changes on track/region edits and song switches.
        if (e.entity == StateEvent::Track || e.entity == StateEvent::Song
            || e.entity == StateEvent::App) {
            rebuild();
            repaint();
        }
    });
    rebuild();
}

AssetsPane::~AssetsPane() {
    if (stateSubId >= 0) state.events().unsubscribe(stateSubId);
}

std::vector<std::pair<float, float>> AssetsPane::downsamplePeaks(
    const std::vector<std::pair<float, float>>& src, int bins) {
    std::vector<std::pair<float, float>> out;
    if (src.empty() || bins <= 0) return out;
    out.reserve(bins);
    for (int i = 0; i < bins; ++i) {
        size_t a = (size_t)((int64_t)i * (int64_t)src.size() / bins);
        size_t b = (size_t)((int64_t)(i + 1) * (int64_t)src.size() / bins);
        if (b <= a) b = a + 1;
        if (b > src.size()) b = src.size();
        float mn = 0.0f, mx = 0.0f;
        for (size_t j = a; j < b; ++j) {
            mn = std::min(mn, src[j].first);
            mx = std::max(mx, src[j].second);
        }
        out.push_back({ mn, mx });
    }
    return out;
}

void AssetsPane::rebuild() {
    assets.clear();
    peaksPending = false;
    auto* song = state.currentSong();
    if (song) {
        std::set<std::string> seen;
        auto consider = [&](const TrackState& t, const RegionState& r) {
            if (r.type != "audio") return;
            for (auto& take : r.takes) {
                if (take.filePath.empty()) continue;
                if (!seen.insert(take.filePath).second) continue;  // dedup by path
                Asset a;
                a.filePath = juce::String(take.filePath);
                a.name = juce::String(t.name);   // owner track name as label
                a.origin = take.origin.empty() ? "unknown" : juce::String(take.origin);
                a.lengthBeats = r.lengthBeats;
                a.recordTempo = take.recordTempo;
                a.sampleRate = take.sampleRate;
                a.channelCount = take.channelCount;
                a.thumb = downsamplePeaks(take.peakData.peaks, thumbBins);
                if (take.peakData.peaks.empty()) peaksPending = true;  // still computing on load
                assets.push_back(std::move(a));
            }
        };
        for (auto& t : song->tracks) {
            for (auto& r : t.regions) consider(t, r);
            for (auto& r : t.loops)   consider(t, r);
        }
        // Loose assets — files that belong to the project but aren't on a track
        // (exported region chunks; later, bounces).
        for (auto& la : song->audioAssets) {
            if (la.filePath.empty()) continue;
            if (!seen.insert(la.filePath).second) continue;
            Asset a;
            a.filePath = juce::String(la.filePath);
            a.name = juce::String(la.name);
            a.origin = la.origin.empty() ? "unknown" : juce::String(la.origin);
            a.lengthBeats = la.lengthBeats;
            a.recordTempo = la.recordTempo;
            a.sampleRate = la.sampleRate;
            a.channelCount = la.channelCount;
            a.thumb = downsamplePeaks(la.peaks, thumbBins);
            if (la.peaks.empty()) peaksPending = true;
            assets.push_back(std::move(a));
        }
    }
    // Peaks for large files finish computing a few seconds after load, with no
    // event to notify us — retry on a low-rate timer until they all land.
    if (peaksPending && isVisible()) startTimer(400);
    else stopTimer();
    layoutRows();
}

void AssetsPane::layoutRows() {
    rows.clear();
    struct Cat { const char* key; const char* label; };
    static const Cat cats[] = {
        { "recorded", "Recorded" }, { "imported", "Imported" }, { "unknown", "Other" }
    };
    int w = getWidth();
    int y = Theme::spacingS;
    for (auto& c : cats) {
        std::vector<int> idxs;
        for (int i = 0; i < (int)assets.size(); ++i)
            if (assets[i].origin == c.key) idxs.push_back(i);
        if (idxs.empty()) continue;

        Row hdr;
        hdr.isHeader = true;
        hdr.headerLabel = c.label;
        hdr.bounds = { 0, y, w, headerRowH };
        rows.push_back(hdr);
        y += headerRowH + rowGap;

        for (int i : idxs) {
            Row r;
            r.assetIndex = i;
            r.bounds = { 0, y, w, assetRowH };
            rows.push_back(r);
            y += assetRowH + rowGap;
        }
    }
    contentHeight = y + Theme::spacingS;
    clampScroll();
}

void AssetsPane::clampScroll() {
    int maxScroll = std::max(0, contentHeight - getHeight());
    scrollY = juce::jlimit(0, maxScroll, scrollY);
}

void AssetsPane::resized() {
    layoutRows();
}

int AssetsPane::rowAtPoint(juce::Point<int> p) const {
    int cy = p.getY() + scrollY;  // content-space
    for (int i = 0; i < (int)rows.size(); ++i)
        if (!rows[i].isHeader && rows[i].bounds.contains(rows[i].bounds.getX() + 1, cy))
            return i;
    return -1;
}

void AssetsPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgPanel));

    if (assets.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText("No audio yet — record or import something.",
                   getLocalBounds().reduced(Theme::spacingM),
                   juce::Justification::centred, true);
        return;
    }

    for (int i = 0; i < (int)rows.size(); ++i) {
        auto& row = rows[i];
        auto b = row.bounds.translated(0, -scrollY);
        if (!b.intersects(getLocalBounds())) continue;

        if (row.isHeader) {
            g.setColour(Theme::color(Theme::Color::textKeyHint));
            g.setFont(Theme::font(Theme::fontSizeSm).boldened());
            g.drawText(row.headerLabel.toUpperCase(),
                       b.reduced(Theme::spacingM, 0).withTrimmedBottom(2),
                       juce::Justification::bottomLeft, false);
        } else {
            paintAssetRow(g, assets[row.assetIndex], b, i == hoverRow);
        }
    }
}

void AssetsPane::paintAssetRow(juce::Graphics& g, const Asset& a,
                               juce::Rectangle<int> b, bool hover) {
    auto inner = b.reduced(Theme::spacingS, 1);
    g.setColour(Theme::color(hover ? Theme::Color::bgControlHover : Theme::Color::bgSlot));
    g.fillRoundedRectangle(inner.toFloat(), Theme::cornerRadiusSm);

    auto content = inner.reduced(Theme::spacingS, Theme::spacingXs);

    // Top line: name (elided) + channel count on the right.
    auto nameRow = content.removeFromTop(15);
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeSm));
    g.drawText(a.name, nameRow, juce::Justification::centredLeft, true);

    // Bottom line: duration + channels, dim. (Tempo omitted — our recordTempo
    // is just the song tempo at capture, not an analyzed property of the audio,
    // so it isn't meaningful for a general asset.)
    double seconds = (a.recordTempo > 0.0) ? a.lengthBeats / a.recordTempo * 60.0 : 0.0;
    int mins = (int)(seconds / 60.0);
    double rem = seconds - mins * 60.0;
    juce::String meta = juce::String::formatted("%d:%05.2f", mins, rem)
                      + "   " + (a.channelCount >= 2 ? "stereo" : "mono");
    auto metaRow = content.removeFromBottom(12);
    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(Theme::fontSizeXs));
    g.drawText(meta, metaRow, juce::Justification::centredLeft, false);

    // Middle: mini-waveform.
    auto wave = content;
    if (!a.thumb.empty() && wave.getHeight() > 2) {
        float midY = wave.getCentreY();
        float halfH = wave.getHeight() * 0.5f - 1.0f;
        int n = (int)a.thumb.size();
        g.setColour(Theme::color(Theme::Color::slotEffect).withAlpha(0.85f));
        for (int i = 0; i < n; ++i) {
            float fx = wave.getX() + (float)i / (float)n * wave.getWidth();
            float mn = juce::jlimit(-1.0f, 1.0f, a.thumb[i].first);
            float mx = juce::jlimit(-1.0f, 1.0f, a.thumb[i].second);
            g.drawVerticalLine((int)fx, midY - mx * halfH, midY - mn * halfH);
        }
    }
}

void AssetsPane::mouseMove(const juce::MouseEvent& e) {
    int r = rowAtPoint(e.getPosition());
    if (r != hoverRow) { hoverRow = r; repaint(); }
}

void AssetsPane::mouseExit(const juce::MouseEvent&) {
    if (hoverRow != -1) { hoverRow = -1; repaint(); }
}

void AssetsPane::mouseDown(const juce::MouseEvent& e) {
    pendingDragRow = rowAtPoint(e.getPosition());
    pendingDragStart = e.getPosition();
}

void AssetsPane::mouseDrag(const juce::MouseEvent& e) {
    if (dragInProgress || pendingDragRow < 0) return;
    if (pendingDragStart.getDistanceFrom(e.getPosition()) <= 5) return;
    if (pendingDragRow >= (int)rows.size() || rows[pendingDragRow].isHeader) return;

    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (!container) { pendingDragRow = -1; return; }

    const auto& a = assets[rows[pendingDragRow].assetIndex];

    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("kind", "asset");
    obj->setProperty("filePath", a.filePath);
    obj->setProperty("origin", a.origin);
    obj->setProperty("name", a.name);

    juce::Image img(juce::Image::ARGB, 160, 22, true);
    {
        juce::Graphics g(img);
        g.setColour(Theme::color(Theme::Color::bgControl).withAlpha(0.92f));
        g.fillRoundedRectangle(img.getBounds().toFloat(), Theme::cornerRadiusSm);
        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(a.name, img.getBounds().reduced(6, 0), juce::Justification::centredLeft, true);
    }

    juce::var payload(obj.release());
    dragInProgress = true;
    container->startDragging(payload, this, juce::ScaledImage(img));
    pendingDragRow = -1;
    dragInProgress = false;
}

void AssetsPane::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    if (contentHeight <= getHeight()) return;
    scrollY -= (int)(w.deltaY * 60.0f);
    clampScroll();
    repaint();
}

void AssetsPane::visibilityChanged() {
    if (isVisible()) { rebuild(); repaint(); }
    else stopTimer();
}

void AssetsPane::timerCallback() {
    // rebuild() re-derives thumbs from (now hopefully populated) peaks and
    // stops this timer once nothing is pending.
    rebuild();
    repaint();
}
