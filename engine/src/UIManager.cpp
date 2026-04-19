#include <arcbit/ui/UIManager.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/input/InputTypes.h>

namespace Arcbit
{
void UIManager::Init(const RenderThread& rt, const FontAtlas& font)
{
    _whiteTex     = rt.GetUIWhiteTexture();
    _whiteSampler = rt.GetUIWhiteSampler();
    _skin.Font    = &font;
}

void UIManager::Push(std::unique_ptr<UIScreen> screen)
{
    screen->OnEnter();
    _stack.push_back(std::move(screen));
}

void UIManager::Pop()
{
    if (_stack.empty()) return;
    _stack.back()->OnExit();
    _stack.pop_back();
}

UIScreen* UIManager::Top() const { return _stack.empty() ? nullptr : _stack.back().get(); }

void UIManager::Update(const f32 dt, const Vec2 windowSize, const InputManager& input)
{
    i32 mx = 0, my = 0;
    input.GetMousePosition(mx, my);
    _mousePos = {static_cast<f32>(mx), static_cast<f32>(my)};

    _mouseDown     = input.IsMouseButtonDown(MouseButton::Left);
    _mouseJustDown = input.IsMouseButtonJustDown(MouseButton::Left);
    _mouseJustUp   = input.IsMouseButtonJustUp(MouseButton::Left);

    if (_stack.empty()) return;

    const UIRect screenRect = {0.0f, 0.0f, windowSize.X, windowSize.Y};

    // Only the top screen receives input.
    _stack.back()->Update(dt, screenRect, _mousePos,
                          _mouseDown, _mouseJustDown, _mouseJustUp);
}

void UIManager::CollectRenderData(FramePacket& packet, const Vec2 windowSize)
{
    if (_stack.empty()) return;

    const UIRect screenRect = {0.0f, 0.0f, windowSize.X, windowSize.Y};

    for (const auto& screen : _stack)
        screen->Collect(packet, screenRect, _skin, _whiteTex, _whiteSampler);
}
} // namespace Arcbit
