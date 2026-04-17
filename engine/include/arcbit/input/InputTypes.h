#pragma once

#include <arcbit/core/Types.h>

#include <string_view>

namespace Arcbit
{

// clang-format off
// ---------------------------------------------------------------------------
// Key — physical keyboard keys (scancode-based, layout-independent).
//
// These map to physical positions on the keyboard, not characters.
// ---------------------------------------------------------------------------
enum class Key : u32
{
    Unknown = 0,

    // --- Letters ------------------------------------------------------------
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // --- Number row ---------------------------------------------------------
    Num0, Num1, Num2, Num3, Num4,
    Num5, Num6, Num7, Num8, Num9,

    // --- Function keys ------------------------------------------------------
    F1, F2, F3, F4,  F5,  F6,
    F7, F8, F9, F10, F11, F12,

    // --- Navigation ---------------------------------------------------------
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,
    Insert, Delete,

    // --- Modifiers ----------------------------------------------------------
    LeftShift,  RightShift,
    LeftCtrl,   RightCtrl,
    LeftAlt,    RightAlt,
    LeftSuper,  RightSuper,   // Windows key / Cmd

    // --- Special ------------------------------------------------------------
    Space, Enter, Escape, Tab, Backspace,
    CapsLock, NumLock, ScrollLock,
    PrintScreen, Pause,

    // --- Punctuation --------------------------------------------------------
    Comma, Period, Slash, Backslash,
    Semicolon, Apostrophe, Grave,
    LeftBracket, RightBracket,
    Minus, Equals,

    // --- Numpad -------------------------------------------------------------
    Kp0, Kp1, Kp2, Kp3, Kp4,
    Kp5, Kp6, Kp7, Kp8, Kp9,
    KpPlus, KpMinus, KpMultiply, KpDivide,
    KpEnter, KpPeriod,

    Count
};

// clang-format on

// ---------------------------------------------------------------------------
// MouseButton
// ---------------------------------------------------------------------------
enum class MouseButton : u8
{
    Left = 0,
    Middle,
    Right,
    X1, // back thumb button (browser back)
    X2, // forward thumb button (browser forward)
    Count
};

// ---------------------------------------------------------------------------
// GamepadButton — platform-neutral face/shoulder/stick button names.
//
// Face buttons use positional names (South/East/West/North) rather than
// brand names (A/B/X/Y or Cross/Circle/Square/Triangle) so the same binding
// works on Xbox, PlayStation, and Switch controllers.
// ---------------------------------------------------------------------------
enum class GamepadButton : u8
{
    South,         // Xbox A  / PS Cross     / Switch B
    East,          // Xbox B  / PS Circle    / Switch A
    West,          // Xbox X  / PS Square    / Switch Y
    North,         // Xbox Y  / PS Triangle  / Switch X
    LeftShoulder,  // Xbox LB / PS L1        / Switch L
    RightShoulder, // Xbox RB / PS R1        / Switch R
    LeftStick,     // Xbox L3 / PS L3        (click left stick)
    RightStick,    // Xbox R3 / PS R3        (click right stick)
    Start,         // Xbox Menu   / PS Options / Switch +
    Back,          // Xbox View   / PS Share   / Switch -
    Guide,         // Xbox / PS button (may not be accessible on all platforms)
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count
};

// ---------------------------------------------------------------------------
// GamepadAxis — analogue axes, normalized to [-1, 1] (triggers: [0, 1]).
// ---------------------------------------------------------------------------
enum class GamepadAxis : u8
{
    LeftX,        // left stick horizontal  (-1 = left,  +1 = right)
    LeftY,        // left stick vertical    (-1 = up,    +1 = down)
    RightX,       // right stick horizontal
    RightY,       // right stick vertical
    LeftTrigger,  // left trigger  [0, 1]
    RightTrigger, // right trigger [0, 1]
    Count
};

// ---------------------------------------------------------------------------
// ActionID
//
// A u32 that identifies an action in the InputManager. Computed at compile
// time from a string name via MakeAction(), so hot-path queries pay no string
// cost — only setup (registration / serialization) uses the name.
//
// Typical usage in game code:
//
//   namespace Actions {
//       constexpr Arcbit::ActionID MoveLeft  = Arcbit::MakeAction("Move_Left");
//       constexpr Arcbit::ActionID MoveRight = Arcbit::MakeAction("Move_Right");
//       constexpr Arcbit::ActionID Jump      = Arcbit::MakeAction("Jump");
//       constexpr Arcbit::ActionID Interact  = Arcbit::MakeAction("Interact");
//   }
//
//   if (input.JustPressed(Actions::Jump)) { ... }
// ---------------------------------------------------------------------------
using ActionID = u32;

// FNV-1a hash — produces the same ActionID for the same string at compile
// time or runtime. Collisions are astronomically unlikely for short names but
// prefer globally unique names (e.g. "Inventory_Open" not "Open") to be safe.
constexpr ActionID MakeAction(const std::string_view name)
{
    u32 hash = 2166136261u; // FNV offset basis
    for (const unsigned char c : name)
    {
        hash ^= c;
        hash *= 16777619u; // FNV prime
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Binding
//
// Associates a physical input (key, button, axis) with an action.
// Use the static factory methods to create bindings rather than filling in
// the struct directly — only the relevant field for each type is meaningful.
//
// Multiple bindings can be added to a single action (e.g. "Jump" → Space key
// OR gamepad South button). The action is considered pressed if ANY binding
// is active.
// ---------------------------------------------------------------------------
struct Binding
{
    enum class Type : u8
    {
        Key,
        MouseButton,
        GamepadButton,
        GamepadAxis
    };

    Type          BindingType = Type::Key;
    Key           BoundKey    = Key::Unknown;
    MouseButton   BoundMouse  = MouseButton::Left;
    GamepadButton BoundButton = GamepadButton::South;
    GamepadAxis   BoundAxis   = GamepadAxis::LeftX;

    // For axis bindings only: the axis must exceed this magnitude to register
    // as pressed. Eliminates stick drift on cheap controllers.
    f32 Deadzone = 0.15f;

    static Binding FromKey(const Key key)
    {
        Binding b;
        b.BindingType = Type::Key;
        b.BoundKey    = key;
        return b;
    }

    static Binding FromMouseButton(const MouseButton button)
    {
        Binding b;
        b.BindingType = Type::MouseButton;
        b.BoundMouse  = button;
        return b;
    }

    static Binding FromGamepadButton(const GamepadButton button)
    {
        Binding b;
        b.BindingType = Type::GamepadButton;
        b.BoundButton = button;
        return b;
    }

    static Binding FromGamepadAxis(const GamepadAxis axis, const f32 deadzone = 0.15f)
    {
        Binding b;
        b.BindingType = Type::GamepadAxis;
        b.BoundAxis   = axis;
        b.Deadzone    = deadzone;
        return b;
    }
};

} // namespace Arcbit
