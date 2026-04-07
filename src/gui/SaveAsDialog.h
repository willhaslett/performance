#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Reusable save-as dialog: text field + clickable list of existing names.
// Click a name to populate the text field, then Save to confirm.
class SaveAsDialog {
public:
    static void show(const juce::String& title,
                     const juce::String& currentName,
                     const juce::StringArray& existingNames,
                     std::function<void(const juce::String&)> onSave) {

        auto* dialog = new juce::DialogWindow(title,
            juce::Colour(0xff1e1e1e), true, true);

        class Content : public juce::Component {
        public:
            juce::TextEditor nameField;
            juce::TextButton saveBtn { "Save" };
            juce::TextButton cancelBtn { "Cancel" };
            juce::ListBox itemList;
            juce::StringArray items;
            std::function<void(const juce::String&)> onSave;
            juce::DialogWindow* owner = nullptr;

            class ListModel : public juce::ListBoxModel {
            public:
                Content* content = nullptr;
                int getNumRows() override { return content ? content->items.size() : 0; }
                void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
                    if (selected)
                        g.fillAll(juce::Colour(0xff2a4a6a));
                    g.setColour(juce::Colour(0xffcccccc));
                    g.setFont(14.0f);
                    if (row >= 0 && row < content->items.size())
                        g.drawText(content->items[row], 8, 0, w - 16, h,
                                   juce::Justification::centredLeft);
                }
                void listBoxItemClicked(int row, const juce::MouseEvent&) override {
                    if (row >= 0 && row < content->items.size())
                        content->nameField.setText(content->items[row]);
                }
            };
            ListModel listModel;

            Content(const juce::String& currentName, const juce::StringArray& names) {
                items = names;
                listModel.content = this;

                nameField.setFont(juce::Font(14.0f));
                nameField.setText(currentName);
                nameField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
                nameField.setColour(juce::TextEditor::textColourId, juce::Colour(0xffffffff));
                nameField.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3a3a3a));
                nameField.setSelectAllWhenFocused(true);
                addAndMakeVisible(nameField);

                itemList.setModel(&listModel);
                itemList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1a1a1a));
                itemList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff3a3a3a));
                itemList.setOutlineThickness(1);
                itemList.setRowHeight(24);
                addAndMakeVisible(itemList);

                saveBtn.onClick = [this] {
                    auto name = nameField.getText().trim();
                    if (name.isNotEmpty() && onSave) onSave(name);
                    if (owner) owner->setVisible(false);
                };
                cancelBtn.onClick = [this] {
                    if (owner) owner->setVisible(false);
                };
                addAndMakeVisible(saveBtn);
                addAndMakeVisible(cancelBtn);

                setSize(300, 280);
            }

            void resized() override {
                auto area = getLocalBounds().reduced(16);
                nameField.setBounds(area.removeFromTop(28));
                area.removeFromTop(8);
                auto buttonArea = area.removeFromBottom(30);
                cancelBtn.setBounds(buttonArea.removeFromRight(80));
                buttonArea.removeFromRight(8);
                saveBtn.setBounds(buttonArea.removeFromRight(80));
                area.removeFromBottom(8);
                itemList.setBounds(area);
            }

            void paint(juce::Graphics& g) override {
                g.fillAll(juce::Colour(0xff1e1e1e));
            }
        };

        auto* content = new Content(currentName, existingNames);
        content->owner = dialog;
        content->onSave = [onSave](const juce::String& name) {
            if (onSave) onSave(name);
        };

        dialog->setContentOwned(content, true);
        dialog->setUsingNativeTitleBar(false);
        dialog->setResizable(false, false);
        dialog->centreWithSize(content->getWidth(),
                                content->getHeight() + dialog->getTitleBarHeight());
        dialog->setVisible(true);
        dialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [dialog](int) { delete dialog; }), false);
    }
};
