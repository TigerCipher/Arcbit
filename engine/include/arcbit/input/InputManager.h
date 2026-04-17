#pragma once

#include <arcbit/input/InputTypes.h>
#include <arcbit/core/Types.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Arcbit {

// ---------------------------------------------------------------------------
// InputManager
//
// Maps physical inputs (keys, mouse buttons, gamepad buttons/axes) to named
// actions, then provides per-frame pressed/just-pressed/just-released queries.
//
// Workflow
// --------
//   1. At startup, register action names and add bindings:
//
//        namespace Actions {
//            constexpr ActionID Jump     = MakeAction("Jump");
//            constexpr ActionID MoveLeft = MakeAction("Move_Left");
//        }
//        input.RegisterAction(Actions::Jump,     "Jump");
//        input.RegisterAction(Actions::MoveLeft, "Move_Left");
//        input.BindKey(Actions::Jump,     Key::Space);
//        input.BindGamepadButton(Actions::Jump, GamepadButton::South);
//        input.BindKey(Actions::MoveLeft, Key::A);
//        input.BindKey(Actions::MoveLeft, Key::Left);
//
//   2. Each frame, call Update() AFTER Window::PollEvents():
//
//        window.PollEvents();   // drains SDL event queue → state is current
//        input.Update();        // polls that state, computes JustPressed etc.
//
//   3. Query actions anywhere in game logic:
//
//        if (input.JustPressed(Actions::Jump))   { player.StartJump(); }
//        f32 dx = input.AxisValue(Actions::MoveLeft);
//
// Thread safety
// -------------
//   NOT thread-safe. Call Update() and all query methods from the same thread
//   (the main/game thread). The render thread never touches InputManager.
//
// Gamepad support
// ---------------
//   InputManager automatically opens newly connected gamepads and closes
//   disconnected ones each time Update() is called — no event handling needed
//   in game code. All connected gamepads are OR'd together: if any gamepad has
//   a binding active, the action is considered pressed.
// ---------------------------------------------------------------------------
class InputManager
{
public:
    InputManager();
    ~InputManager();

    // InputManager is non-copyable (owns SDL_Gamepad handles).
    InputManager(const InputManager&)            = delete;
    InputManager& operator=(const InputManager&) = delete;

    // -----------------------------------------------------------------------
    // Registration
    //
    // RegisterAction links a human-readable name to an ActionID for
    // serialization (Settings System, Lua). Calling it is optional — actions
    // work without names, but the settings screen needs them to display and
    // save bindings.
    // -----------------------------------------------------------------------
    void RegisterAction(ActionID action, std::string_view name);

    // -----------------------------------------------------------------------
    // Binding
    //
    // Multiple bindings can be added per action — the action is pressed if
    // ANY binding is active. Call ClearBindings first when rebinding in the
    // settings screen to replace rather than accumulate.
    // -----------------------------------------------------------------------
    void BindKey          (ActionID action, Key           key);
    void BindMouseButton  (ActionID action, MouseButton   button);
    void BindGamepadButton(ActionID action, GamepadButton button);
    void BindGamepadAxis  (ActionID action, GamepadAxis   axis, f32 deadzone = 0.15f);

    // Remove all bindings for an action without unregistering it.
    void ClearBindings(ActionID action);

    // -----------------------------------------------------------------------
    // Input event injection
    //
    // Called by Application to forward raw SDL events captured during
    // Window::PollEvents(). Parameters are opaque ints matching the SDL types
    // (SDL_Scancode, SDL button index, SDL_GamepadButton) without requiring
    // SDL headers here.
    //
    // Events accumulate in pending sets between ProcessEdges() calls so they
    // are never lost if the fixed-timestep loop fires zero times in a frame.
    // -----------------------------------------------------------------------
    void InjectKeyEvent     (int scancode,   bool down);
    void InjectMouseButton  (int sdlButton,  bool down);
    void InjectGamepadButton(u32 joystickId, int sdlButton, bool down);

    // -----------------------------------------------------------------------
    // Per-frame update
    //
    // Must be called once per display frame, AFTER Window::PollEvents().
    // Polls current keyboard/mouse/gamepad state for IsPressed and AxisValue.
    // Does NOT set JustPressed/JustReleased — call ProcessEdges() for those.
    // -----------------------------------------------------------------------
    void Update();

    // -----------------------------------------------------------------------
    // Per-tick edge processing
    //
    // Call once per game tick (inside the fixed-timestep loop), after Update().
    // Consumes accumulated key/mouse/gamepad button events and sets
    // JustPressed / JustReleased for all actions, then clears the event queue.
    //
    // Why separate from Update()?
    //   Update() runs every display frame; the fixed-timestep OnUpdate() may
    //   run 0 or N times per display frame. Putting edge detection here ensures
    //   events are held until a tick consumes them — no JustPressed is ever lost
    //   because the accumulator was slightly short of the threshold.
    // -----------------------------------------------------------------------
    void ProcessEdges();

    // -----------------------------------------------------------------------
    // Action queries
    // -----------------------------------------------------------------------

    // True every frame that at least one binding for this action is held.
    [[nodiscard]] bool IsPressed    (ActionID action) const;

    // True for exactly ONE frame — the frame the action first becomes pressed.
    [[nodiscard]] bool JustPressed  (ActionID action) const;

    // True for exactly ONE frame — the frame all bindings are released.
    [[nodiscard]] bool JustReleased (ActionID action) const;

    // Analogue value in [-1, 1] (axes) or 0/1 (buttons/keys).
    // For actions with multiple bindings, returns the value with the largest
    // absolute magnitude.
    [[nodiscard]] f32  AxisValue    (ActionID action) const;

    // -----------------------------------------------------------------------
    // Mouse queries
    // -----------------------------------------------------------------------

    // Absolute cursor position in window pixels (top-left origin).
    void GetMousePosition(i32& outX, i32& outY) const;

    // Pixels the cursor moved since the previous Update() call.
    void GetMouseDelta(i32& outDx, i32& outDy) const;

    // -----------------------------------------------------------------------
    // Serialization helpers (consumed by Settings System in Phase 10)
    // -----------------------------------------------------------------------

    // Human-readable name registered for this action, or "" if unregistered.
    [[nodiscard]] std::string_view GetActionName(ActionID action) const;

    // Read-only view of all bindings for an action (for serialization/display).
    [[nodiscard]] const std::vector<Binding>& GetBindings(ActionID action) const;

    // All registered action IDs — used by the Settings System to enumerate
    // actions when saving/loading the input binding file.
    [[nodiscard]] std::vector<ActionID> GetAllActions() const;

private:
    // Per-action data stored in the registry.
    struct ActionEntry
    {
        std::string       Name;     // registered name (may be empty)
        std::vector<Binding> Bindings;

        // State computed by Update() each frame.
        bool Pressed      = false;
        bool JustPressed  = false;
        bool JustReleased = false;
        f32  AxisValue    = 0.0f;
    };

    // Returns or creates an entry for the given action ID.
    ActionEntry& GetOrCreate(ActionID action);

    // Evaluate whether a single binding is active this frame. Returns the
    // analogue value (0/1 for digital, -1..1 for axes).
    [[nodiscard]] f32 EvaluateBinding(const Binding& binding) const;

    // --- SDL state (populated by Update) ------------------------------------

    // SDL keyboard state array — pointer into SDL's internal buffer, valid
    // until the next SDL_PollEvent / SDL_PumpEvents call. We capture it in
    // Update() and use it for the duration of that call only.
    // SDL3 changed this from Uint8* (SDL2) to bool* (SDL3).
    const bool* m_KeyState = nullptr;

    // SDL mouse state bitmask and position.
    u32 m_MouseButtonMask = 0;
    i32 m_MouseX = 0, m_MouseY = 0;
    i32 m_PrevMouseX = 0, m_PrevMouseY = 0;

    // Open gamepad handles. Keys are SDL_JoystickID (opaque u32 in SDL3).
    // Stored as void* to keep SDL3/SDL.h out of this header.
    std::unordered_map<u32, void*> m_Gamepads; // SDL_JoystickID → SDL_Gamepad*

    // --- Action registry ----------------------------------------------------
    std::unordered_map<ActionID, ActionEntry> m_Actions;

    // Returned by GetBindings when the action has no entry, so we never
    // return a dangling reference.
    static const std::vector<Binding> s_EmptyBindings;

    // --- Pending input events (accumulated between ProcessEdges() calls) ----
    // Populated by InjectKeyEvent / InjectMouseButton / InjectGamepadButton.
    // ProcessEdges() reads these to set JustPressed/JustReleased, then clears them.

    struct PendingGamepadEvent { u32 JoystickId; int Button; };

    std::unordered_set<int>           m_PendingKeyDown;
    std::unordered_set<int>           m_PendingKeyUp;
    std::unordered_set<int>           m_PendingMouseDown;
    std::unordered_set<int>           m_PendingMouseUp;
    std::vector<PendingGamepadEvent>  m_PendingGamepadDown;
    std::vector<PendingGamepadEvent>  m_PendingGamepadUp;
};

} // namespace Arcbit
