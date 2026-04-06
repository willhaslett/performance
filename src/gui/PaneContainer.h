#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Divider.h"
#include <vector>
#include <memory>

// Manages N vertically-split panes with draggable dividers between them.
// Panes store proportional widths (fractions summing to 1.0) so they
// scale with window resizes.
class PaneContainer : public juce::Component {
public:
    void addPane(juce::Component* comp, float proportion) {
        panes.push_back({ comp, proportion, true });
        addAndMakeVisible(comp);
        rebuildDividers();
        resized();
    }

    void setPaneVisible(juce::Component* comp, bool visible) {
        for (auto& p : panes) {
            if (p.component == comp) {
                p.visible = visible;
                comp->setVisible(visible);
                rebuildDividers();
                resized();
                return;
            }
        }
    }

    bool isPaneVisible(juce::Component* comp) const {
        for (auto& p : panes)
            if (p.component == comp) return p.visible;
        return false;
    }

    void resized() override {
        auto visiblePanes = getVisiblePanes();
        if (visiblePanes.empty()) return;

        int numDividers = (int)visiblePanes.size() - 1;
        int availableWidth = getWidth() - numDividers * Divider::thickness;
        if (availableWidth <= 0) return;

        // Normalize proportions
        float totalProp = 0;
        for (auto* p : visiblePanes) totalProp += p->proportion;
        if (totalProp <= 0) totalProp = 1.0f;

        int x = 0;
        int divIdx = 0;

        for (int i = 0; i < (int)visiblePanes.size(); ++i) {
            int paneWidth;
            if (i == (int)visiblePanes.size() - 1) {
                // Last pane gets remaining to avoid rounding gaps
                paneWidth = getWidth() - x;
            } else {
                paneWidth = (int)(visiblePanes[i]->proportion / totalProp * (float)availableWidth);
                paneWidth = std::max(minPaneWidth, paneWidth);
            }

            visiblePanes[i]->component->setBounds(x, 0, paneWidth, getHeight());
            x += paneWidth;

            // Place divider after this pane (if not last)
            if (i < numDividers && divIdx < (int)dividers.size()) {
                dividers[divIdx]->setBounds(x, 0, Divider::thickness, getHeight());
                dividers[divIdx]->toFront(false);
                x += Divider::thickness;
                divIdx++;
            }
        }
    }

private:
    struct Pane {
        juce::Component* component;
        float proportion;
        bool visible = true;
    };

    std::vector<Pane> panes;
    std::vector<std::unique_ptr<Divider>> dividers;
    static constexpr int minPaneWidth = 100;

    std::vector<Pane*> getVisiblePanes() {
        std::vector<Pane*> result;
        for (auto& p : panes)
            if (p.visible) result.push_back(&p);
        return result;
    }

    void rebuildDividers() {
        // Remove old dividers
        for (auto& d : dividers)
            removeChildComponent(d.get());
        dividers.clear();

        auto visiblePanes = getVisiblePanes();
        if (visiblePanes.size() < 2) return;

        // Create one divider between each pair of visible panes
        for (int i = 0; i < (int)visiblePanes.size() - 1; ++i) {
            auto divider = std::make_unique<Divider>(Divider::Vertical);
            auto* leftPane = visiblePanes[i];
            auto* rightPane = visiblePanes[i + 1];

            divider->onDragStart = [this, leftPane, rightPane]() {
                dragStartLeft = leftPane->proportion;
                dragStartRight = rightPane->proportion;
            };

            divider->onDrag = [this, leftPane, rightPane](int delta) {
                auto visP = getVisiblePanes();
                float totalProp = 0;
                for (auto* p : visP) totalProp += p->proportion;

                int numDiv = (int)visP.size() - 1;
                int availW = getWidth() - numDiv * Divider::thickness;
                if (availW <= 0) return;

                float propDelta = (float)delta / (float)availW * totalProp;

                float newLeft = dragStartLeft + propDelta;
                float newRight = dragStartRight - propDelta;

                // Enforce minimum widths
                float minProp = (float)minPaneWidth / (float)availW * totalProp;
                if (newLeft < minProp || newRight < minProp) return;

                leftPane->proportion = newLeft;
                rightPane->proportion = newRight;
                resized();
            };

            addAndMakeVisible(*divider);
            dividers.push_back(std::move(divider));
        }
    }

    float dragStartLeft = 0;
    float dragStartRight = 0;
};
