#pragma once

#include <arcbit/ui/UIWidget.h>
#include <arcbit/ui/UISkin.h>

#include <memory>
#include <vector>

namespace Arcbit {

struct FramePacket;

// ---------------------------------------------------------------------------
// UIScreen — a full-screen root widget container.
//
// Screens are stacked by UIManager.  Only the top screen receives input by
// default; all visible screens collect their render data each frame.
// ---------------------------------------------------------------------------
class UIScreen
{
public:
    virtual ~UIScreen() = default;

    // Called once when the screen is pushed onto the stack.
    virtual void OnEnter() {}

    // Called once when the screen is popped off the stack.
    virtual void OnExit() {}

    // Add a root-level widget. Returns raw ptr; ownership stays here.
    template<typename T, typename... Args>
    T* Add(Args&&... args)
    {
        auto w = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = w.get();
        _roots.push_back(std::move(w));
        return ptr;
    }

    // Drive the widget tree for this frame.
    void Update(f32 dt, UIRect screenRect, Vec2 mousePos,
                bool mouseDown, bool mouseJustDown, bool mouseJustUp);

    // Emit all quads into the packet.
    void Collect(FramePacket& packet, UIRect screenRect, const UISkin& skin,
                 TextureHandle whiteTex, SamplerHandle whiteSampler);

    [[nodiscard]] bool IsVisible() const  { return _visible; }
    void SetVisible(const bool v) { _visible = v; }

protected:
    std::vector<std::unique_ptr<UIWidget>> _roots;

private:
    bool _visible = true;
};

} // namespace Arcbit
