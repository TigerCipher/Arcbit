#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/UISkin.h>

#include <memory>
#include <vector>

namespace Arcbit {

struct FramePacket;
class  InputManager;
class  RenderThread;

// ---------------------------------------------------------------------------
// UIManager — owns the screen stack, drives update/collect each frame.
//
// Push screens to show them; pop to dismiss.  The top-most screen receives
// mouse input; all visible screens are collected for rendering.
// ---------------------------------------------------------------------------
class UIManager
{
public:
    // Must be called once after the render device is ready.
    void Init(const RenderThread& rt, const FontAtlas& font);

    // Replace the default skin colors/font.
    void SetSkin(const UISkin& skin) { _skin = skin; }
    [[nodiscard]] const UISkin& GetSkin() const { return _skin; }

    void Push(std::unique_ptr<UIScreen> screen);
    void Pop();

    [[nodiscard]] UIScreen* Top() const;
    [[nodiscard]] bool      Empty() const { return _stack.empty(); }

    // Called every game frame from Application::Update.
    void Update(f32 dt, Vec2 windowSize, const InputManager& input);

    // Emit all UI quads into the frame packet.
    void CollectRenderData(FramePacket& packet, Vec2 windowSize);

private:
    std::vector<std::unique_ptr<UIScreen>> _stack;
    UISkin          _skin;
    TextureHandle   _whiteTex;
    SamplerHandle   _whiteSampler;

    // Cached per-frame input (set in Update, read in CollectRenderData if needed).
    Vec2  _mousePos       = {0, 0};
    bool  _mouseDown      = false;
    bool  _mouseJustDown  = false;
    bool  _mouseJustUp    = false;
};

} // namespace Arcbit
