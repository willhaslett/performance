#pragma once
#include "state/StateModel.h"
#include <deque>

// Snapshot-based undo/redo. Stores full AppState copies.
// Push before each user mutation. Undo restores the previous snapshot.
// Disabled during recording (re-enabled on stop, with recording result as current state).

class UndoHistory {
public:
    static constexpr int maxSteps = 50;

    void push(const AppState& state) {
        if (suspended) return;
        // Trim redo future
        redoStack.clear();
        undoStack.push_back(state);
        if ((int)undoStack.size() > maxSteps)
            undoStack.pop_front();
    }

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    // Returns the state to restore to. Caller must apply it.
    AppState undo(const AppState& current) {
        if (undoStack.empty()) return current;
        redoStack.push_back(current);
        auto restored = std::move(undoStack.back());
        undoStack.pop_back();
        return restored;
    }

    AppState redo(const AppState& current) {
        if (redoStack.empty()) return current;
        undoStack.push_back(current);
        auto restored = std::move(redoStack.back());
        redoStack.pop_back();
        return restored;
    }

    void clear() {
        undoStack.clear();
        redoStack.clear();
    }

    void suspend() { suspended = true; }
    void resume() { suspended = false; }
    bool isSuspended() const { return suspended; }

private:
    std::deque<AppState> undoStack;
    std::deque<AppState> redoStack;
    bool suspended = false;
};
