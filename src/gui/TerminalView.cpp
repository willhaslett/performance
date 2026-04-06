#include "gui/TerminalView.h"
#include "gui/Theme.h"
#include "engine/Log.h"
#include <util.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

// libvterm callback: output bytes to pty
static void vtermOutput(const char* s, size_t len, void* user) {
    auto* tv = static_cast<TerminalView*>(user);
    tv->sendBytes(s, len);
}

TerminalView::TerminalView() {
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

TerminalView::~TerminalView() {
    stop();
    if (vt) vterm_free(vt);
}

void TerminalView::start(const juce::String& command, const juce::String& workingDir) {
    stop();

    // Create vterm
    if (vt) vterm_free(vt);
    vt = vterm_new(termRows, termCols);
    vterm_set_utf8(vt, 1);

    vterm_output_set_callback(vt, vtermOutput, this);

    vtScreen = vterm_obtain_screen(vt);
    vterm_screen_reset(vtScreen, 1);

    // Fork with pty
    struct winsize ws;
    ws.ws_col = (unsigned short)termCols;
    ws.ws_row = (unsigned short)termRows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    childPid = forkpty(&ptyFd, nullptr, nullptr, &ws);

    if (childPid == 0) {
        if (workingDir.isNotEmpty())
            chdir(workingDir.toRawUTF8());
        setenv("TERM", "xterm-256color", 1);
        setenv("LANG", "en_US.UTF-8", 1);
        execl("/bin/zsh", "zsh", "-c", command.toRawUTF8(), nullptr);
        _exit(1);
    }

    if (childPid < 0) {
        perfLog("[Terminal] Failed to fork\n");
        return;
    }

    running = true;
    readThread = std::thread([this] { readLoop(); });
    perfLog("[Terminal] Started process (pid %d), %dx%d\n", childPid, termCols, termRows);
}

void TerminalView::stop() {
    running = false;
    if (childPid > 0) {
        kill(childPid, SIGTERM);
        waitpid(childPid, nullptr, WNOHANG);
        childPid = 0;
    }
    if (ptyFd >= 0) {
        close(ptyFd);
        ptyFd = -1;
    }
    if (readThread.joinable())
        readThread.join();
}

void TerminalView::readLoop() {
    char buf[4096];
    while (running) {
        ssize_t n = read(ptyFd, buf, sizeof(buf));
        if (n <= 0) break;
        std::lock_guard<std::mutex> lock(vtMutex);
        vterm_input_write(vt, buf, (size_t)n);
    }
    running = false;
}

void TerminalView::sendBytes(const char* data, size_t len) {
    if (ptyFd >= 0)
        write(ptyFd, data, len);
}

void TerminalView::timerCallback() {
    repaint();
}

juce::Colour TerminalView::vtColorToJuce(VTermColor color, bool isFg) const {
    if (VTERM_COLOR_IS_DEFAULT_FG(&color))
        return Theme::color(Theme::Color::textPrimary);
    if (VTERM_COLOR_IS_DEFAULT_BG(&color))
        return Theme::color(Theme::Color::bgApp);
    if (VTERM_COLOR_IS_INDEXED(&color)) {
        vterm_screen_convert_color_to_rgb(vtScreen, &color);
    }
    if (VTERM_COLOR_IS_RGB(&color))
        return juce::Colour(color.rgb.red, color.rgb.green, color.rgb.blue);
    return isFg ? Theme::color(Theme::Color::textPrimary) : Theme::color(Theme::Color::bgApp);
}

void TerminalView::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    if (!vt || !vtScreen) return;

    std::lock_guard<std::mutex> lock(vtMutex);

    int cellW = 8;
    int cellH = 16;

    auto font = juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, 0));
    g.setFont(font);

    for (int row = 0; row < termRows; ++row) {
        for (int col = 0; col < termCols; ++col) {
            VTermPos pos = { row, col };
            VTermScreenCell cell;
            vterm_screen_get_cell(vtScreen, pos, &cell);

            int x = col * cellW;
            int y = row * cellH;

            // Background
            auto bgColor = vtColorToJuce(cell.bg, false);
            if (bgColor != Theme::color(Theme::Color::bgApp)) {
                g.setColour(bgColor);
                g.fillRect(x, y, cellW * cell.width, cellH);
            }

            // Character
            if (cell.chars[0] != 0 && cell.chars[0] != ' ') {
                auto fgColor = vtColorToJuce(cell.fg, true);
                g.setColour(fgColor);

                // Handle bold
                if (cell.attrs.bold)
                    g.setFont(juce::Font(juce::FontOptions(
                        juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold)));

                char utf8[8] = {};
                int len = 0;
                uint32_t ch = cell.chars[0];
                if (ch < 0x80) {
                    utf8[0] = (char)ch;
                    len = 1;
                } else if (ch < 0x800) {
                    utf8[0] = (char)(0xC0 | (ch >> 6));
                    utf8[1] = (char)(0x80 | (ch & 0x3F));
                    len = 2;
                } else if (ch < 0x10000) {
                    utf8[0] = (char)(0xE0 | (ch >> 12));
                    utf8[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
                    utf8[2] = (char)(0x80 | (ch & 0x3F));
                    len = 3;
                } else {
                    utf8[0] = (char)(0xF0 | (ch >> 18));
                    utf8[1] = (char)(0x80 | ((ch >> 12) & 0x3F));
                    utf8[2] = (char)(0x80 | ((ch >> 6) & 0x3F));
                    utf8[3] = (char)(0x80 | (ch & 0x3F));
                    len = 4;
                }

                g.drawText(juce::String::fromUTF8(utf8, len),
                           x, y, cellW * cell.width, cellH,
                           juce::Justification::centredLeft, false);

                if (cell.attrs.bold)
                    g.setFont(font);
            }

            // Skip wide char continuation cells
            if (cell.width > 1)
                col += cell.width - 1;
        }
    }

    // Draw cursor
    VTermPos cursorPos;
    vterm_state_get_cursorpos(vterm_obtain_state(vt), &cursorPos);
    g.setColour(juce::Colour(0xaaffffff));
    g.fillRect(cursorPos.col * cellW, cursorPos.row * cellH, 2, cellH);
}

void TerminalView::resized() {
    updateTermSize();
}

void TerminalView::updateTermSize() {
    int cellW = 8;
    int cellH = 16;
    int newCols = std::max(10, getWidth() / cellW);
    int newRows = std::max(4, getHeight() / cellH);

    if (newCols != termCols || newRows != termRows) {
        termCols = newCols;
        termRows = newRows;

        if (vt) {
            std::lock_guard<std::mutex> lock(vtMutex);
            vterm_set_size(vt, termRows, termCols);
        }

        if (ptyFd >= 0) {
            struct winsize ws;
            ws.ws_col = (unsigned short)termCols;
            ws.ws_row = (unsigned short)termRows;
            ws.ws_xpixel = 0;
            ws.ws_ypixel = 0;
            ioctl(ptyFd, TIOCSWINSZ, &ws);
        }
    }
}

bool TerminalView::keyPressed(const juce::KeyPress& key) {
    if (!running || ptyFd < 0) return false;

    auto mods = key.getModifiers();

    // Special keys
    if (key.getKeyCode() == juce::KeyPress::returnKey) {
        sendBytes("\r", 1);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::backspaceKey) {
        sendBytes("\x7f", 1);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::tabKey) {
        sendBytes("\t", 1);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::escapeKey) {
        sendBytes("\x1b", 1);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::upKey) {
        sendBytes("\x1b[A", 3);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::downKey) {
        sendBytes("\x1b[B", 3);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::rightKey) {
        sendBytes("\x1b[C", 3);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey) {
        sendBytes("\x1b[D", 3);
        return true;
    }

    // Ctrl+key
    if (mods.isCtrlDown()) {
        char c = (char)(key.getKeyCode() & 0x1F);
        sendBytes(&c, 1);
        return true;
    }

    // Regular character
    auto c = key.getTextCharacter();
    if (c != 0) {
        char utf8[8] = {};
        int len = 0;
        if (c < 0x80) {
            utf8[0] = (char)c;
            len = 1;
        } else if (c < 0x800) {
            utf8[0] = (char)(0xC0 | (c >> 6));
            utf8[1] = (char)(0x80 | (c & 0x3F));
            len = 2;
        } else {
            utf8[0] = (char)(0xE0 | (c >> 12));
            utf8[1] = (char)(0x80 | ((c >> 6) & 0x3F));
            utf8[2] = (char)(0x80 | (c & 0x3F));
            len = 3;
        }
        sendBytes(utf8, len);
        return true;
    }

    return false;
}

void TerminalView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    // Could implement scrollback here later
}
