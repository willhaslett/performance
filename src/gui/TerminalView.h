#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vterm.h>
#include <thread>
#include <atomic>
#include <mutex>

class TerminalView : public juce::Component, private juce::Timer {
public:
    TerminalView();
    ~TerminalView() override;

    void start(const juce::String& command, const juce::String& workingDir);
    void stop();

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

    // PTY
    int ptyFd = -1;
    pid_t childPid = 0;
    std::thread readThread;
    std::atomic<bool> running { false };
    void readLoop();

    // libvterm
    VTerm* vt = nullptr;
    VTermScreen* vtScreen = nullptr;
    int termRows = 24;
    int termCols = 80;
    std::mutex vtMutex;

    int scrollbackOffset = 0;

    // Convert VTerm color to JUCE colour
    juce::Colour vtColorToJuce(VTermColor color, bool isFg) const;

    void updateTermSize();

public:
    void sendBytes(const char* data, size_t len);
};
