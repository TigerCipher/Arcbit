#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/input/InputTypes.h>

#include <memory>
#include <vector>

namespace Arcbit {

struct FramePacket;
class  InputManager;
class  RenderThread;

// ---------------------------------------------------------------------------
// UIManager — owns the screen stack, drives update/collect each frame.
//
// Push screens to show them; Pop starts a fade-out before removal.
// All visible screens are collected (rendered); only the topmost non-fading
// screen receives input.
// ---------------------------------------------------------------------------
class UIManager
{
public:
    // Pre-registered UI navigation actions — bound to defaults in Init().
    // Games may rebind these via Settings the same as any other action.
    static constexpr ActionID ActionConfirm   = MakeAction("UI_Confirm");
    static constexpr ActionID ActionFocusNext = MakeAction("UI_FocusNext");
    static constexpr ActionID ActionFocusPrev = MakeAction("UI_FocusPrev");

    // Must be called once after the render device is ready.
    // Registers navigation actions and binds their default keys.
    void Init(const RenderThread& rt, const FontAtlas& font, InputManager& input);

    // Preserves the current font pointer when the incoming skin has none,
    // since JSON skins cannot serialize FontAtlas pointers.
    void SetSkin(const UISkin& skin)
    {
        const FontAtlas* prev = _skin.Font;
        _skin = skin;
        if (!_skin.Font) _skin.Font = prev;
    }
    [[nodiscard]] const UISkin& GetSkin() const { return _skin; }

    void Push(std::unique_ptr<UIScreen> screen);
    void Pop();

    [[nodiscard]] UIScreen* Top()   const;
    [[nodiscard]] bool      Empty() const { return _stack.empty(); }

    // Returns true if any screen on the stack has BlocksInput = true.
    // Use this in OnUpdate to suppress game input when a menu is open.
    [[nodiscard]] bool HasBlockingScreen() const;

    void Update(f32 dt, Vec2 windowSize, const InputManager& input);
    void CollectRenderData(FramePacket& packet, Vec2 windowSize);

private:
    std::vector<std::unique_ptr<UIScreen>> _stack;
    UISkin        _skin;
    TextureHandle _whiteTex;
    SamplerHandle _whiteSampler;

    Vec2 _mousePos      = {0, 0};
    bool _mouseDown     = false;
    bool _mouseJustDown = false;
    bool _mouseJustUp   = false;

    // Returns the topmost screen that is not fading out (receives input).
    [[nodiscard]] UIScreen* ActiveInputScreen() const;

    // Remove screens that have completed their fade-out.
    void RemoveCompletedFadeOuts();
};

} // namespace Arcbit
