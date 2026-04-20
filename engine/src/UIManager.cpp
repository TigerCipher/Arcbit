#include <arcbit/ui/UIManager.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/input/InputTypes.h>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Init — register nav actions and bind default keys/buttons.
// ---------------------------------------------------------------------------

void UIManager::Init(const RenderThread& rt, const FontAtlas& font, InputManager& input)
{
    _whiteTex     = rt.GetUIWhiteTexture();
    _whiteSampler = rt.GetUIWhiteSampler();
    _skin.Font    = &font;

    input.RegisterAction(ActionConfirm,   "UI_Confirm");
    input.RegisterAction(ActionFocusNext, "UI_FocusNext");
    input.RegisterAction(ActionFocusPrev, "UI_FocusPrev");

    input.BindKey(ActionConfirm,   Key::Enter);
    input.BindKey(ActionFocusNext, Key::Tab);
    input.BindKey(ActionFocusNext, Key::Down);
    input.BindKey(ActionFocusNext, Key::Right);
    input.BindKey(ActionFocusPrev, Key::Up);
    input.BindKey(ActionFocusPrev, Key::Left);

    input.BindGamepadButton(ActionConfirm,   GamepadButton::South);
    input.BindGamepadButton(ActionFocusNext, GamepadButton::DPadDown);
    input.BindGamepadButton(ActionFocusNext, GamepadButton::DPadRight);
    input.BindGamepadButton(ActionFocusPrev, GamepadButton::DPadUp);
    input.BindGamepadButton(ActionFocusPrev, GamepadButton::DPadLeft);
}

// ---------------------------------------------------------------------------
// Screen stack
// ---------------------------------------------------------------------------

void UIManager::Push(std::unique_ptr<UIScreen> screen)
{
    if (screen->TransitionSpeed > 0.0f) {
        screen->_transitionOpacity = 0.0f;
        screen->_transitionState   = UIScreen::TransitionState::FadingIn;
    }
    screen->OnEnter();
    _stack.push_back(std::move(screen));
}

void UIManager::Pop()
{
    if (_stack.empty()) return;
    UIScreen& top = *_stack.back();
    if (top.TransitionSpeed > 0.0f) {
        top._transitionOpacity = 1.0f;
        top._transitionState   = UIScreen::TransitionState::FadingOut;
    } else {
        top.OnExit();
        _stack.pop_back();
    }
}

UIScreen* UIManager::Top() const
{
    return _stack.empty() ? nullptr : _stack.back().get();
}

bool UIManager::HasBlockingScreen() const
{
    for (const auto& s : _stack)
        if (s->BlocksInput) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

UIScreen* UIManager::ActiveInputScreen() const
{
    for (auto it = _stack.rbegin(); it != _stack.rend(); ++it)
        if (!(*it)->IsFadingOut()) return it->get();
    return nullptr;
}

void UIManager::RemoveCompletedFadeOuts()
{
    for (auto it = _stack.begin(); it != _stack.end(); ) {
        if ((*it)->IsFadingOut() && (*it)->GetTransitionOpacity() <= 0.0f) {
            (*it)->OnExit();
            it = _stack.erase(it);
        } else {
            ++it;
        }
    }
}

void UIManager::Update(const f32 dt, const Vec2 windowSize, const InputManager& input)
{
    i32 mx = 0, my = 0;
    input.GetMousePosition(mx, my);
    _mousePos      = { static_cast<f32>(mx), static_cast<f32>(my) };
    _mouseDown     = input.IsMouseButtonDown(MouseButton::Left);
    _mouseJustDown = input.IsMouseButtonJustDown(MouseButton::Left);
    _mouseJustUp   = input.IsMouseButtonJustUp(MouseButton::Left);

    for (auto& s : _stack) s->AdvanceTransition(dt);
    RemoveCompletedFadeOuts();

    UIScreen* active = ActiveInputScreen();
    if (!active) return;

    const UIRect screenRect = { 0.0f, 0.0f, windowSize.X, windowSize.Y };
    active->Update(dt, screenRect, _mousePos, _mouseDown, _mouseJustDown, _mouseJustUp);

    if (input.JustPressed(ActionFocusNext)) active->FocusNext();
    if (input.JustPressed(ActionFocusPrev)) active->FocusPrev();
    if (input.JustPressed(ActionConfirm))   active->ActivateFocused();
}

// ---------------------------------------------------------------------------
// CollectRenderData
// ---------------------------------------------------------------------------

void UIManager::CollectRenderData(FramePacket& packet, const Vec2 windowSize)
{
    if (_stack.empty()) return;
    const UIRect screenRect = { 0.0f, 0.0f, windowSize.X, windowSize.Y };
    for (const auto& screen : _stack)
        screen->Collect(packet, screenRect, _skin, _whiteTex, _whiteSampler);
}

} // namespace Arcbit
