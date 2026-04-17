#include <arcbit/app/Window.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

// SDL is included only in this translation unit — the public header stays clean.
#include <SDL3/SDL.h>

namespace Arcbit {

// Convenience cast — SDL_Window* travels as void* through the public API.
static SDL_Window* ToSDL(void* handle) { return static_cast<SDL_Window*>(handle); }

Window::Window(const Desc& desc)
{
    ARCBIT_VERIFY(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "SDL_Init failed");

    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
    if (desc.Resizable)
        flags |= SDL_WINDOW_RESIZABLE;

    SDL_Window* win = SDL_CreateWindow(desc.Title,
        static_cast<int>(desc.Width),
        static_cast<int>(desc.Height),
        flags);

    ARCBIT_ASSERT(win != nullptr, "SDL_CreateWindow failed");

    m_Window = win;
    m_Width  = desc.Width;
    m_Height = desc.Height;

    LOG_INFO(Platform, "Window created: '{}' ({}x{})", desc.Title, desc.Width, desc.Height);
}

Window::~Window()
{
    if (m_Window)
        SDL_DestroyWindow(ToSDL(m_Window));

    SDL_Quit();
    LOG_INFO(Platform, "Window destroyed");
}

bool Window::PollEvents()
{
    // Clear the resize flag at the start of every frame — it should only be
    // true for the single frame in which the resize event was received.
    m_ResizedThisFrame = false;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                return false;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE)
                    return false;
                // Suppress repeat events — only forward physical press transitions.
                if (!event.key.repeat && m_KeyEventFn)
                    m_KeyEventFn(static_cast<int>(event.key.scancode), true);
                break;

            case SDL_EVENT_KEY_UP:
                if (m_KeyEventFn)
                    m_KeyEventFn(static_cast<int>(event.key.scancode), false);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (m_MouseButtonFn)
                    m_MouseButtonFn(static_cast<int>(event.button.button), true);
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (m_MouseButtonFn)
                    m_MouseButtonFn(static_cast<int>(event.button.button), false);
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (m_GamepadButtonFn)
                    m_GamepadButtonFn(static_cast<u32>(event.gbutton.which),
                                      static_cast<int>(event.gbutton.button), true);
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (m_GamepadButtonFn)
                    m_GamepadButtonFn(static_cast<u32>(event.gbutton.which),
                                      static_cast<int>(event.gbutton.button), false);
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                m_Width            = static_cast<u32>(event.window.data1);
                m_Height           = static_cast<u32>(event.window.data2);
                m_ResizedThisFrame = true;
                LOG_DEBUG(Platform, "Window resized to {}x{}", m_Width, m_Height);
                break;

            default:
                break;
        }
    }

    return true; // window is still open
}

void Window::SetKeyEventCallback(std::function<void(int, bool)> fn)
{
    m_KeyEventFn = std::move(fn);
}

void Window::SetMouseButtonCallback(std::function<void(int, bool)> fn)
{
    m_MouseButtonFn = std::move(fn);
}

void Window::SetGamepadButtonCallback(std::function<void(u32, int, bool)> fn)
{
    m_GamepadButtonFn = std::move(fn);
}

void* Window::GetNativeHandle() const
{
    return m_Window;
}

} // namespace Arcbit
