#include <arcbit/input/InputManager.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <ranges>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Static members
// ---------------------------------------------------------------------------

const std::vector<Binding> InputManager::s_EmptyBindings;

// ---------------------------------------------------------------------------
// SDL mapping helpers
//
// These convert our engine enums to SDL constants. Keeping them here (not in
// the header) means SDL3/SDL.h stays out of the public API entirely.
// ---------------------------------------------------------------------------

namespace {

// Map Key → SDL_Scancode (physical keyboard position, layout-independent).
SDL_Scancode ToScancode(const Key key)
{
    switch (key)
    {
        // Letters
        case Key::A: return SDL_SCANCODE_A; case Key::B: return SDL_SCANCODE_B;
        case Key::C: return SDL_SCANCODE_C; case Key::D: return SDL_SCANCODE_D;
        case Key::E: return SDL_SCANCODE_E; case Key::F: return SDL_SCANCODE_F;
        case Key::G: return SDL_SCANCODE_G; case Key::H: return SDL_SCANCODE_H;
        case Key::I: return SDL_SCANCODE_I; case Key::J: return SDL_SCANCODE_J;
        case Key::K: return SDL_SCANCODE_K; case Key::L: return SDL_SCANCODE_L;
        case Key::M: return SDL_SCANCODE_M; case Key::N: return SDL_SCANCODE_N;
        case Key::O: return SDL_SCANCODE_O; case Key::P: return SDL_SCANCODE_P;
        case Key::Q: return SDL_SCANCODE_Q; case Key::R: return SDL_SCANCODE_R;
        case Key::S: return SDL_SCANCODE_S; case Key::T: return SDL_SCANCODE_T;
        case Key::U: return SDL_SCANCODE_U; case Key::V: return SDL_SCANCODE_V;
        case Key::W: return SDL_SCANCODE_W; case Key::X: return SDL_SCANCODE_X;
        case Key::Y: return SDL_SCANCODE_Y; case Key::Z: return SDL_SCANCODE_Z;

        // Number row
        case Key::Num0: return SDL_SCANCODE_0; case Key::Num1: return SDL_SCANCODE_1;
        case Key::Num2: return SDL_SCANCODE_2; case Key::Num3: return SDL_SCANCODE_3;
        case Key::Num4: return SDL_SCANCODE_4; case Key::Num5: return SDL_SCANCODE_5;
        case Key::Num6: return SDL_SCANCODE_6; case Key::Num7: return SDL_SCANCODE_7;
        case Key::Num8: return SDL_SCANCODE_8; case Key::Num9: return SDL_SCANCODE_9;

        // Function keys
        case Key::F1:  return SDL_SCANCODE_F1;  case Key::F2:  return SDL_SCANCODE_F2;
        case Key::F3:  return SDL_SCANCODE_F3;  case Key::F4:  return SDL_SCANCODE_F4;
        case Key::F5:  return SDL_SCANCODE_F5;  case Key::F6:  return SDL_SCANCODE_F6;
        case Key::F7:  return SDL_SCANCODE_F7;  case Key::F8:  return SDL_SCANCODE_F8;
        case Key::F9:  return SDL_SCANCODE_F9;  case Key::F10: return SDL_SCANCODE_F10;
        case Key::F11: return SDL_SCANCODE_F11; case Key::F12: return SDL_SCANCODE_F12;

        // Navigation
        case Key::Left:     return SDL_SCANCODE_LEFT;
        case Key::Right:    return SDL_SCANCODE_RIGHT;
        case Key::Up:       return SDL_SCANCODE_UP;
        case Key::Down:     return SDL_SCANCODE_DOWN;
        case Key::Home:     return SDL_SCANCODE_HOME;
        case Key::End:      return SDL_SCANCODE_END;
        case Key::PageUp:   return SDL_SCANCODE_PAGEUP;
        case Key::PageDown: return SDL_SCANCODE_PAGEDOWN;
        case Key::Insert:   return SDL_SCANCODE_INSERT;
        case Key::Delete:   return SDL_SCANCODE_DELETE;

        // Modifiers
        case Key::LeftShift:  return SDL_SCANCODE_LSHIFT;
        case Key::RightShift: return SDL_SCANCODE_RSHIFT;
        case Key::LeftCtrl:   return SDL_SCANCODE_LCTRL;
        case Key::RightCtrl:  return SDL_SCANCODE_RCTRL;
        case Key::LeftAlt:    return SDL_SCANCODE_LALT;
        case Key::RightAlt:   return SDL_SCANCODE_RALT;
        case Key::LeftSuper:  return SDL_SCANCODE_LGUI;
        case Key::RightSuper: return SDL_SCANCODE_RGUI;

        // Special
        case Key::Space:       return SDL_SCANCODE_SPACE;
        case Key::Enter:       return SDL_SCANCODE_RETURN;
        case Key::Escape:      return SDL_SCANCODE_ESCAPE;
        case Key::Tab:         return SDL_SCANCODE_TAB;
        case Key::Backspace:   return SDL_SCANCODE_BACKSPACE;
        case Key::CapsLock:    return SDL_SCANCODE_CAPSLOCK;
        case Key::NumLock:     return SDL_SCANCODE_NUMLOCKCLEAR;
        case Key::ScrollLock:  return SDL_SCANCODE_SCROLLLOCK;
        case Key::PrintScreen: return SDL_SCANCODE_PRINTSCREEN;
        case Key::Pause:       return SDL_SCANCODE_PAUSE;

        // Punctuation
        case Key::Comma:        return SDL_SCANCODE_COMMA;
        case Key::Period:       return SDL_SCANCODE_PERIOD;
        case Key::Slash:        return SDL_SCANCODE_SLASH;
        case Key::Backslash:    return SDL_SCANCODE_BACKSLASH;
        case Key::Semicolon:    return SDL_SCANCODE_SEMICOLON;
        case Key::Apostrophe:   return SDL_SCANCODE_APOSTROPHE;
        case Key::Grave:        return SDL_SCANCODE_GRAVE;
        case Key::LeftBracket:  return SDL_SCANCODE_LEFTBRACKET;
        case Key::RightBracket: return SDL_SCANCODE_RIGHTBRACKET;
        case Key::Minus:        return SDL_SCANCODE_MINUS;
        case Key::Equals:       return SDL_SCANCODE_EQUALS;

        // Numpad
        case Key::Kp0:        return SDL_SCANCODE_KP_0;
        case Key::Kp1:        return SDL_SCANCODE_KP_1;
        case Key::Kp2:        return SDL_SCANCODE_KP_2;
        case Key::Kp3:        return SDL_SCANCODE_KP_3;
        case Key::Kp4:        return SDL_SCANCODE_KP_4;
        case Key::Kp5:        return SDL_SCANCODE_KP_5;
        case Key::Kp6:        return SDL_SCANCODE_KP_6;
        case Key::Kp7:        return SDL_SCANCODE_KP_7;
        case Key::Kp8:        return SDL_SCANCODE_KP_8;
        case Key::Kp9:        return SDL_SCANCODE_KP_9;
        case Key::KpPlus:     return SDL_SCANCODE_KP_PLUS;
        case Key::KpMinus:    return SDL_SCANCODE_KP_MINUS;
        case Key::KpMultiply: return SDL_SCANCODE_KP_MULTIPLY;
        case Key::KpDivide:   return SDL_SCANCODE_KP_DIVIDE;
        case Key::KpEnter:    return SDL_SCANCODE_KP_ENTER;
        case Key::KpPeriod:   return SDL_SCANCODE_KP_PERIOD;

        default: return SDL_SCANCODE_UNKNOWN;
    }
}

// Map MouseButton → the SDL button index used by SDL_BUTTON_MASK.
// SDL3 mouse button indices: Left=1, Middle=2, Right=3, X1=4, X2=5.
int ToSDLMouseButton(const MouseButton button)
{
    switch (button)
    {
        case MouseButton::Left:   return SDL_BUTTON_LEFT;
        case MouseButton::Middle: return SDL_BUTTON_MIDDLE;
        case MouseButton::Right:  return SDL_BUTTON_RIGHT;
        case MouseButton::X1:     return SDL_BUTTON_X1;
        case MouseButton::X2:     return SDL_BUTTON_X2;
        default:                  return SDL_BUTTON_LEFT;
    }
}

SDL_GamepadButton ToSDLGamepadButton(const GamepadButton button)
{
    switch (button)
    {
        case GamepadButton::South:         return SDL_GAMEPAD_BUTTON_SOUTH;
        case GamepadButton::East:          return SDL_GAMEPAD_BUTTON_EAST;
        case GamepadButton::West:          return SDL_GAMEPAD_BUTTON_WEST;
        case GamepadButton::North:         return SDL_GAMEPAD_BUTTON_NORTH;
        case GamepadButton::LeftShoulder:  return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
        case GamepadButton::RightShoulder: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        case GamepadButton::LeftStick:     return SDL_GAMEPAD_BUTTON_LEFT_STICK;
        case GamepadButton::RightStick:    return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
        case GamepadButton::Start:         return SDL_GAMEPAD_BUTTON_START;
        case GamepadButton::Back:          return SDL_GAMEPAD_BUTTON_BACK;
        case GamepadButton::Guide:         return SDL_GAMEPAD_BUTTON_GUIDE;
        case GamepadButton::DPadUp:        return SDL_GAMEPAD_BUTTON_DPAD_UP;
        case GamepadButton::DPadDown:      return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        case GamepadButton::DPadLeft:      return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
        case GamepadButton::DPadRight:     return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        default:                           return SDL_GAMEPAD_BUTTON_SOUTH;
    }
}

SDL_GamepadAxis ToSDLGamepadAxis(const GamepadAxis axis)
{
    switch (axis)
    {
        case GamepadAxis::LeftX:         return SDL_GAMEPAD_AXIS_LEFTX;
        case GamepadAxis::LeftY:         return SDL_GAMEPAD_AXIS_LEFTY;
        case GamepadAxis::RightX:        return SDL_GAMEPAD_AXIS_RIGHTX;
        case GamepadAxis::RightY:        return SDL_GAMEPAD_AXIS_RIGHTY;
        case GamepadAxis::LeftTrigger:   return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        case GamepadAxis::RightTrigger:  return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        default:                         return SDL_GAMEPAD_AXIS_LEFTX;
    }
}

// Normalize a raw SDL axis value (Sint16, range -32768..32767) to [-1, 1].
// Triggers (LeftTrigger / RightTrigger) report [0..32767], so they map to
// [0, 1] naturally after this normalization.
f32 NormalizeAxis(const i16 raw)
{
    return raw >= 0
        ? static_cast<f32>(raw)  / 32767.0f
        : static_cast<f32>(raw)  / 32768.0f;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

InputManager::InputManager() = default;

InputManager::~InputManager()
{
    // Close all open gamepad handles so SDL can release its resources.
    for (auto &handle: m_Gamepads | std::views::values)
        SDL_CloseGamepad(static_cast<SDL_Gamepad*>(handle));
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void InputManager::RegisterAction(const ActionID action, const std::string_view name)
{
    auto& entry = GetOrCreate(action);
    entry.Name  = std::string(name);
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

void InputManager::BindKey(const ActionID action, const Key key)
{
    GetOrCreate(action).Bindings.push_back(Binding::FromKey(key));
}

void InputManager::BindMouseButton(const ActionID action, const MouseButton button)
{
    GetOrCreate(action).Bindings.push_back(Binding::FromMouseButton(button));
}

void InputManager::BindGamepadButton(const ActionID action, const GamepadButton button)
{
    GetOrCreate(action).Bindings.push_back(Binding::FromGamepadButton(button));
}

void InputManager::BindGamepadAxis(const ActionID action, const GamepadAxis axis, const f32 deadzone)
{
    GetOrCreate(action).Bindings.push_back(Binding::FromGamepadAxis(axis, deadzone));
}

void InputManager::ClearBindings(const ActionID action)
{
    if (const auto it = m_Actions.find(action); it != m_Actions.end())
        it->second.Bindings.clear();
}

// ---------------------------------------------------------------------------
// Input event injection — called by Application during Window::PollEvents
// ---------------------------------------------------------------------------

void InputManager::InjectKeyEvent(const int scancode, const bool down)
{
    if (down) m_PendingKeyDown.insert(scancode);
    else       m_PendingKeyUp.insert(scancode);
}

void InputManager::InjectMouseButton(const int sdlButton, const bool down)
{
    if (down) m_PendingMouseDown.insert(sdlButton);
    else       m_PendingMouseUp.insert(sdlButton);
}

void InputManager::InjectGamepadButton(const u32 joystickId, const int sdlButton, const bool down)
{
    if (down) m_PendingGamepadDown.push_back({ joystickId, sdlButton });
    else       m_PendingGamepadUp.push_back({ joystickId, sdlButton });
}

// ---------------------------------------------------------------------------
// Update — called once per display frame after Window::PollEvents
// ---------------------------------------------------------------------------

void InputManager::Update()
{
    // --- Keyboard -----------------------------------------------------------
    // SDL_GetKeyboardState returns a pointer to an internal array. We capture
    // it here; the pointer stays valid until the next SDL_PumpEvents call, but
    // we only use it within this function so that's fine.
    int numKeys = 0;
    m_KeyState  = SDL_GetKeyboardState(&numKeys);

    // --- Mouse --------------------------------------------------------------
    m_PrevMouseX = m_MouseX;
    m_PrevMouseY = m_MouseY;

    // SDL3: SDL_GetMouseState returns a bitmask of pressed buttons and writes
    // the cursor position into the provided floats.
    float mx = 0.0f, my = 0.0f;
    m_MouseButtonMask = SDL_GetMouseState(&mx, &my);
    m_MouseX = static_cast<i32>(mx);
    m_MouseY = static_cast<i32>(my);

    // --- Gamepad connect / disconnect ---------------------------------------
    // SDL_GetGamepads allocates and returns the list of currently connected
    // gamepad IDs. We diff against m_Gamepads to open new ones and close
    // removed ones — no event processing required.
    int gamepadCount = 0;

    if (SDL_JoystickID* connectedIDs = SDL_GetGamepads(&gamepadCount))
    {
        // Build a set of currently connected IDs for O(n) comparison.
        // (Gamepad counts are tiny so std::vector is fine here.)
        std::vector<u32> currentIDs(connectedIDs, connectedIDs + gamepadCount);
        SDL_free(connectedIDs);

        // Open newly connected gamepads.
        for (u32 id : currentIDs)
        {
            if (!m_Gamepads.contains(id))
            {
                if (SDL_Gamepad* gp = SDL_OpenGamepad(id))
                {
                    m_Gamepads[id] = gp;
                    LOG_INFO(Platform, "Gamepad connected: {} (id={})",
                        SDL_GetGamepadName(gp), id);
                }
            }
        }

        // Close disconnected gamepads.
        for (auto it = m_Gamepads.begin(); it != m_Gamepads.end(); )
        {
            const bool stillConnected = std::ranges::find(currentIDs,
                                                    it->first) != currentIDs.end();
            if (!stillConnected)
            {
                LOG_INFO(Platform, "Gamepad disconnected (id={})", it->first);
                SDL_CloseGamepad(static_cast<SDL_Gamepad*>(it->second));
                it = m_Gamepads.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // --- Evaluate actions ---------------------------------------------------
    // Compute IsPressed and AxisValue from current SDL state.
    // JustPressed / JustReleased are NOT set here — ProcessEdges() handles
    // those from the accumulated event queue, called once per game tick.
    for (auto& entry : m_Actions | std::views::values)
    {
        bool anyPressed  = false;
        f32  bestAxisVal = 0.0f;

        for (const Binding& b : entry.Bindings)
        {
            const f32 val = EvaluateBinding(b);
            if (std::abs(val) > std::abs(bestAxisVal))
                bestAxisVal = val;
            if (val != 0.0f)
                anyPressed = true;
        }

        entry.Pressed   = anyPressed;
        entry.AxisValue = bestAxisVal;
        // Reset edge flags — ProcessEdges() will set them correctly for each tick.
        entry.JustPressed  = false;
        entry.JustReleased = false;
    }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool InputManager::IsPressed(const ActionID action) const
{
    const auto it = m_Actions.find(action);
    return it != m_Actions.end() && it->second.Pressed;
}

bool InputManager::JustPressed(const ActionID action) const
{
    const auto it = m_Actions.find(action);
    return it != m_Actions.end() && it->second.JustPressed;
}

bool InputManager::JustReleased(const ActionID action) const
{
    const auto it = m_Actions.find(action);
    return it != m_Actions.end() && it->second.JustReleased;
}

f32 InputManager::AxisValue(const ActionID action) const
{
    const auto it = m_Actions.find(action);
    return it != m_Actions.end() ? it->second.AxisValue : 0.0f;
}

void InputManager::ProcessEdges()
{
    for (auto& entry : m_Actions | std::views::values)
    {
        bool anyJustPressed  = false;
        bool anyJustReleased = false;

        for (const Binding& b : entry.Bindings)
        {
            switch (b.BindingType)
            {
                case Binding::Type::Key:
                {
                    const int sc = static_cast<int>(ToScancode(b.BoundKey));
                    if (m_PendingKeyDown.contains(sc)) anyJustPressed  = true;
                    if (m_PendingKeyUp.contains(sc))   anyJustReleased = true;
                    break;
                }
                case Binding::Type::MouseButton:
                {
                    const int btn = ToSDLMouseButton(b.BoundMouse);
                    if (m_PendingMouseDown.contains(btn)) anyJustPressed  = true;
                    if (m_PendingMouseUp.contains(btn))   anyJustReleased = true;
                    break;
                }
                case Binding::Type::GamepadButton:
                {
                    const int sdlBtn = static_cast<int>(ToSDLGamepadButton(b.BoundButton));
                    for (const auto& [id, btn] : m_PendingGamepadDown)
                        if (btn == sdlBtn) { anyJustPressed = true; break; }
                    for (const auto& [id, btn] : m_PendingGamepadUp)
                        if (btn == sdlBtn) { anyJustReleased = true; break; }
                    break;
                }
                default:
                    break;
            }
        }

        // JustReleased is only meaningful when the action is no longer pressed.
        entry.JustPressed  = anyJustPressed;
        entry.JustReleased = anyJustReleased && !entry.Pressed;
    }

    m_PendingKeyDown.clear();
    m_PendingKeyUp.clear();
    m_PendingMouseDown.clear();
    m_PendingMouseUp.clear();
    m_PendingGamepadDown.clear();
    m_PendingGamepadUp.clear();
}

void InputManager::GetMousePosition(i32& outX, i32& outY) const
{
    outX = m_MouseX;
    outY = m_MouseY;
}

void InputManager::GetMouseDelta(i32& outDx, i32& outDy) const
{
    outDx = m_MouseX - m_PrevMouseX;
    outDy = m_MouseY - m_PrevMouseY;
}

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------

std::string_view InputManager::GetActionName(const ActionID action) const
{
    const auto it = m_Actions.find(action);
    return it != m_Actions.end() ? std::string_view(it->second.Name) : std::string_view{};
}

const std::vector<Binding>& InputManager::GetBindings(const ActionID action) const
{
    const auto it = m_Actions.find(action);
    return it != m_Actions.end() ? it->second.Bindings : s_EmptyBindings;
}

std::vector<ActionID> InputManager::GetAllActions() const
{
    std::vector<ActionID> result;
    result.reserve(m_Actions.size());
    for (const auto& [id, _] : m_Actions)
        result.push_back(id);
    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

InputManager::ActionEntry& InputManager::GetOrCreate(const ActionID action)
{
    return m_Actions[action]; // default-constructs if not present
}

f32 InputManager::EvaluateBinding(const Binding& binding) const
{
    switch (binding.BindingType)
    {
        // ----- Keyboard -----------------------------------------------------
        case Binding::Type::Key:
        {
            if (!m_KeyState) return 0.0f;
            const SDL_Scancode sc = ToScancode(binding.BoundKey);
            return (sc != SDL_SCANCODE_UNKNOWN && m_KeyState[sc]) ? 1.0f : 0.0f;
        }

        // ----- Mouse button -------------------------------------------------
        case Binding::Type::MouseButton:
        {
            // SDL_GetMouseState returns a bitmask; SDL_BUTTON_MASK converts
            // a button index to the corresponding bit position.
            const int sdlBtn = ToSDLMouseButton(binding.BoundMouse);
            return (m_MouseButtonMask & SDL_BUTTON_MASK(sdlBtn)) ? 1.0f : 0.0f;
        }

        // ----- Gamepad button -----------------------------------------------
        case Binding::Type::GamepadButton:
        {
            const SDL_GamepadButton sdlBtn = ToSDLGamepadButton(binding.BoundButton);
            // Any connected gamepad having the button pressed counts.
            for (const auto &handle: m_Gamepads | std::views::values)
            {
                if (SDL_GetGamepadButton(static_cast<SDL_Gamepad*>(handle), sdlBtn))
                    return 1.0f;
            }
            return 0.0f;
        }

        // ----- Gamepad axis -------------------------------------------------
        case Binding::Type::GamepadAxis:
        {
            const SDL_GamepadAxis sdlAxis = ToSDLGamepadAxis(binding.BoundAxis);
            f32 best = 0.0f;
            for (const auto &handle: m_Gamepads | std::views::values)
            {
                const i16 raw  = SDL_GetGamepadAxis(static_cast<SDL_Gamepad*>(handle), sdlAxis);
                f32 norm = NormalizeAxis(raw);
                // Apply deadzone — values within [-deadzone, deadzone] are zero.
                if (std::abs(norm) < binding.Deadzone)
                    norm = 0.0f;
                if (std::abs(norm) > std::abs(best))
                    best = norm;
            }
            return best;
        }
    }

    return 0.0f;
}

} // namespace Arcbit
