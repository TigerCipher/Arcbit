#pragma once

#include <arcbit/core/Types.h>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Window
//
// RAII wrapper around an SDL_Window. Owns the SDL video subsystem for its
// lifetime — SDL_Init is called in the constructor, SDL_Quit in the destructor.
//
// SDL3 is the single cross-platform windowing layer for all backends:
//   - Vulkan backend calls SDL_Vulkan_CreateSurface on the SDL_Window*.
//   - A future D3D12 backend would extract the HWND via SDL_GetPointerProperty.
// Neither backend type requires its own window abstraction — the variation
// point is in RenderDevice, not here.
//
// SDL types are intentionally kept out of this header so consumers don't need
// to include SDL3/SDL.h. GetNativeHandle() returns the SDL_Window* as void*.
//
// Usage:
//   Window::Desc desc{ .Title = "My Game", .Width = 1280, .Height = 720 };
//   Window window(desc);
//
//   while (window.PollEvents())
//   {
//       if (window.WasResizedThisFrame()) { /* recreate swapchain */ }
//       // build and submit FramePacket ...
//   }
// ---------------------------------------------------------------------------
class Window
{
public:
    struct Desc
    {
        const char* Title     = "Arcbit";
        u32         Width     = 1280;
        u32         Height    = 720;
        bool        Resizable = true;
    };

    explicit Window(const Desc& desc);
    ~Window();

    // Non-copyable — owns an OS window handle.
    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    // ---------------------------------------------------------------------------
    // Per-frame update
    //
    // Drains the SDL event queue. Returns false when the window has been closed
    // (SDL_EVENT_QUIT received) or Escape was pressed, signalling the game loop
    // to exit. Window dimensions and the resize flag are updated here.
    //
    // Must be called from the thread that created the window (SDL requirement).
    // ---------------------------------------------------------------------------
    bool PollEvents();

    // ---------------------------------------------------------------------------
    // Accessors
    // ---------------------------------------------------------------------------

    // Current client area dimensions. Updated inside PollEvents on resize.
    [[nodiscard]] constexpr u32 GetWidth()  const { return m_Width; }
    [[nodiscard]] constexpr u32 GetHeight() const { return m_Height; }

    // True if a SDL_EVENT_WINDOW_RESIZED event was processed in the most recent
    // PollEvents call. Cleared at the start of every PollEvents call, so it is
    // only ever true for one frame — the frame the resize was detected.
    [[nodiscard]] constexpr bool WasResizedThisFrame() const { return m_ResizedThisFrame; }

    // Returns the underlying SDL_Window* as void* so that render backends can
    // cast it back without this header pulling in SDL3/SDL.h.
    // RenderDevice and SwapchainDesc accept this as NativeWindowHandle.
    [[nodiscard]] void* GetNativeHandle() const;

private:
    // SDL_Window* stored as void* to avoid including SDL3/SDL.h in this header.
    // Cast back to SDL_Window* in Window.cpp where SDL is included.
    void* m_Window           = nullptr;

    u32   m_Width            = 0;
    u32   m_Height           = 0;
    bool  m_ResizedThisFrame = false;
};

} // namespace Arcbit
