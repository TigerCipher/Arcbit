#pragma once

#include <arcbit/ui/UIWidget.h>
#include <arcbit/ui/UISkin.h>

#include <memory>
#include <vector>

namespace Arcbit {

struct FramePacket;
class  InputManager;

// ---------------------------------------------------------------------------
// UIScreen — a full-screen root widget container.
//
// Screens are stacked by UIManager. The top-most screen receives input; all
// visible screens are collected each frame.  Push transitions fade in; Pop
// transitions fade out before the screen is removed from the stack.
// ---------------------------------------------------------------------------
class UIScreen
{
public:
    virtual ~UIScreen() = default;

    virtual void OnEnter() {}
    virtual void OnExit()  {}

    // Called once per game tick before widget UpdateTree.
    // Override in screens that need direct input access (e.g. InputRebindScreen).
    virtual void OnTick(f32 /*dt*/, const InputManager& /*input*/) {}

    // Rate at which transition opacity changes (units/second). 0 = instant.
    f32 TransitionSpeed = 3.0f;

    // When true, UIManager::HasBlockingScreen() returns true while this screen
    // is on the stack. Use for pause menus / dialogs; leave false for HUDs.
    bool BlocksInput = false;

    // Add a root-level widget. Returns raw ptr; ownership stays here.
    template<typename T, typename... Args>
    T* Add(Args&&... args)
    {
        auto w = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = w.get();
        _roots.push_back(std::move(w));
        return ptr;
    }

    void Update(f32 dt, UIRect screenRect, Vec2 mousePos,
                bool mouseDown, bool mouseJustDown, bool mouseJustUp, f32 scrollDelta = 0.0f);

    void Collect(FramePacket& packet, UIRect screenRect, const UISkin& skin,
                 TextureHandle whiteTex, SamplerHandle whiteSampler);

    // --- Focus navigation ---------------------------------------------------

    void FocusNext();
    void FocusPrev();
    void ActivateFocused();
    void ClearFocus();

    [[nodiscard]] UIWidget* GetFocusedWidget() const { return _focusedWidget; }

    // --- Visibility / transition state --------------------------------------

    [[nodiscard]] bool IsVisible()       const { return _visible; }
    void               SetVisible(bool v)      { _visible = v; }

    [[nodiscard]] bool IsFadingOut()          const { return _transitionState == TransitionState::FadingOut; }
    [[nodiscard]] f32  GetTransitionOpacity() const { return _transitionOpacity; }

protected:
    std::vector<std::unique_ptr<UIWidget>> _roots;

private:
    enum class TransitionState { Idle, FadingIn, FadingOut };

    bool            _visible           = true;
    TransitionState _transitionState   = TransitionState::Idle;
    f32             _transitionOpacity = 1.0f;
    UIWidget*       _focusedWidget     = nullptr;

    void AdvanceTransition(f32 dt);
    void ApplyFocus(UIWidget* widget);

    std::vector<UIWidget*> GatherFocusables() const;
    static void CollectFocusables(UIWidget& w, std::vector<UIWidget*>& out);

    friend class UIManager;
};

} // namespace Arcbit
