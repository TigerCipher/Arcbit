#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/render/RenderHandle.h>
#include <arcbit/input/InputTypes.h>

#include <functional>
#include <string>
#include <vector>

namespace Arcbit
{
class  InputManager;
class  Label;
class  Button;
class  ScrollPanel;

// ---------------------------------------------------------------------------
// InputRebindScreen — engine-provided input binding editor.
//
// Shows all registered actions with two binding columns: keyboard and gamepad.
// Clicking a column button enters listening mode for that input type — the
// next matching input replaces that slot.  Click the same button again to
// cancel.  Actions whose names start with a prefix in HiddenPrefixes are
// omitted (hides internal engine actions by default).
//
// Requires Input to be set before Push.  Wire OnBack to pop the screen.
// ---------------------------------------------------------------------------
class InputRebindScreen : public UIScreen
{
public:
    InputManager* Input = nullptr;

    std::function<void()> OnBack;

    // Action names starting with any of these prefixes are hidden.
    std::vector<std::string> HiddenPrefixes = {"UI_", "Engine_"};

    InputRebindScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;
    void OnTick(f32 dt, const InputManager& input) override;

private:
    struct ActionRow
    {
        ActionID action      = 0;
        Label*   nameLabel   = nullptr;
        Button*  keyButton   = nullptr;   // keyboard binding column
        Button*  ctrlButton  = nullptr;   // gamepad binding column
        bool     listening   = false;
        bool     listenKey   = true;      // true = waiting for key, false = gamepad
    };

    std::vector<ActionRow> _rows;
    i32  _listeningIdx  = -1;
    f32  _flashTimer    = 0.0f;
    ScrollPanel* _scroll = nullptr;

    void RebuildRows();
    void StartListening(i32 idx, bool forKey);
    void StopListening();
    void RefreshKeyText (ActionRow& row) const;
    void RefreshCtrlText(ActionRow& row) const;
};
} // namespace Arcbit
