#include <arcbit/render/RenderThread.h>
#include <arcbit/core/Log.h>

#include <array>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

void RenderThread::Start(RenderDevice* device)
{
    m_Device  = device;

    // Set m_Running before spawning the thread so the thread never sees a
    // false value on first entry into Run().
    m_Running = true;

    // std::thread immediately begins executing Run() on the new OS thread.
    // We store the thread object so we can join it later in Stop().
    m_Thread = std::thread(&RenderThread::Run, this);

    LOG_INFO(Render, "Render thread started");
}

void RenderThread::Stop()
{
    // Write m_Running = false under the lock so the render thread's wait
    // condition sees a consistent value — there is no window where the thread
    // checks m_Running, finds it true, then we set it false and call notify
    // before the thread has called wait.
    {
        std::lock_guard lock(m_Mutex);
        m_Running = false;
    }

    // Wake the thread in case it is sleeping on m_FrameReady.
    // If it is mid-frame it will see m_Running == false at the *next* iteration.
    m_FrameReady.notify_one();

    // Block until the render thread has exited cleanly.
    // After join() returns, no Vulkan calls from that thread are in flight.
    if (m_Thread.joinable())
        m_Thread.join();

    LOG_INFO(Render, "Render thread stopped");
}

// ---------------------------------------------------------------------------
// Frame submission (game thread side)
// ---------------------------------------------------------------------------

void RenderThread::SubmitFrame(FramePacket packet)
{
    std::unique_lock lock(m_Mutex);

    // Back-pressure: wait until the render thread has finished the previous frame.
    // m_SlotFree starts true, so the first call goes straight through.
    //
    // std::condition_variable::wait(lock, predicate) is equivalent to:
    //   while (!predicate()) m_FrameDone.wait(lock);
    // It re-checks the predicate after every wake to guard against spurious
    // wakeups (the OS is allowed to wake a thread without a notify).
    m_FrameDone.wait(lock, [this] { return m_SlotFree; });

    // Move the packet into shared storage. std::move avoids copying the
    // DrawCalls vector — ownership transfers in O(1).
    m_Packet   = std::move(packet);
    m_HasFrame = true;
    m_SlotFree = false;

    // Unlock before notifying: the render thread can be woken and immediately
    // reacquire the lock without having to wait for us to release it first.
    lock.unlock();
    m_FrameReady.notify_one();
}

// ---------------------------------------------------------------------------
// Render thread loop
// ---------------------------------------------------------------------------

void RenderThread::Run()
{
    // This loop runs for the lifetime of the application on the render thread.
    while (true)
    {
        FramePacket packet;

        // Sleep until the game thread submits a frame or Stop() is called.
        {
            std::unique_lock lock(m_Mutex);
            m_FrameReady.wait(lock, [this] { return m_HasFrame || !m_Running; });

            // Stop() sets m_Running = false then notifies. If there is a
            // pending frame we render it first so the last game frame is
            // always displayed. If not, exit immediately.
            if (!m_Running && !m_HasFrame)
                break;

            // Take ownership of the packet so the game thread can write the
            // *next* packet while we are rendering this one. This is the key
            // benefit of double-buffering: both threads do useful work in
            // parallel rather than taking turns.
            packet     = std::move(m_Packet);
            m_HasFrame = false;
        }
        // Lock is released here — game thread may now call SubmitFrame again,
        // but it will block on m_FrameDone until we set m_SlotFree below.

        RenderFrame(packet);

        // Signal the game thread that the packet slot is free.
        // Lock, write, unlock, notify — same careful ordering as SubmitFrame.
        {
            std::lock_guard lock(m_Mutex);
            m_SlotFree = true;
        }
        m_FrameDone.notify_one();
    }
}

// ---------------------------------------------------------------------------
// Single-frame rendering (render thread side)
// ---------------------------------------------------------------------------

void RenderThread::RenderFrame(const FramePacket& packet)
{
    // --- Swapchain resize ---------------------------------------------------
    //
    // Process a resize *before* acquiring an image. If we acquired first, the
    // acquire might return VK_ERROR_OUT_OF_DATE_KHR and we'd have already
    // waited on the in-flight fence — wasted work. Resizing proactively avoids
    // that in the common case.
    if (packet.NeedsResize)
        m_Device->ResizeSwapchain(packet.Swapchain, packet.Width, packet.Height);

    // --- Acquire backbuffer -------------------------------------------------
    //
    // AcquireNextImage blocks on the per-frame-slot in-flight fence (guaranteeing
    // the GPU is done with this slot's resources) then asks the driver for the
    // next swapchain image. Returns an invalid handle if the swapchain is
    // out-of-date (e.g. a resize the driver noticed before we did).
    TextureHandle backbuffer = m_Device->AcquireNextImage(packet.Swapchain);

    if (!backbuffer.IsValid())
    {
        // Swapchain went out of date between the resize check and the acquire
        // (can happen with rapid back-to-back resize events). Recreate and retry.
        LOG_WARN(Render, "Swapchain out of date at acquire — recreating and retrying");
        m_Device->ResizeSwapchain(packet.Swapchain, packet.Width, packet.Height);
        backbuffer = m_Device->AcquireNextImage(packet.Swapchain);

        if (!backbuffer.IsValid())
        {
            // Still invalid after recreation — surface is in an unusable state
            // (e.g. window minimised to zero size on some platforms). Skip this
            // frame entirely; the game thread will retry next tick.
            LOG_ERROR(Render, "Failed to acquire image after swapchain recreation — skipping frame");
            return;
        }
    }

    // --- Command recording --------------------------------------------------

    CommandListHandle cmd = m_Device->BeginCommandList();

    // Set up the colour attachment with the requested clear colour.
    Attachment colorAttach{};
    colorAttach.Texture       = backbuffer;
    colorAttach.Load          = LoadOp::Clear;
    colorAttach.Store         = StoreOp::Store;
    colorAttach.ClearColor[0] = packet.ClearColor[0];
    colorAttach.ClearColor[1] = packet.ClearColor[1];
    colorAttach.ClearColor[2] = packet.ClearColor[2];
    colorAttach.ClearColor[3] = packet.ClearColor[3];

    std::array<Attachment, 1> attachments = { colorAttach };
    RenderingDesc renderDesc{};
    renderDesc.ColorAttachments = attachments;

    m_Device->BeginRendering(cmd, renderDesc);

    // Execute each draw call in submission order. The game thread is
    // responsible for ordering (e.g. opaque before transparent).
    for (const DrawCall& dc : packet.DrawCalls)
    {
        m_Device->BindPipeline(cmd, dc.Pipeline);

        // Full-viewport/scissor covering the current backbuffer dimensions.
        m_Device->SetViewport(cmd, 0.0f, 0.0f,
            static_cast<f32>(packet.Width),
            static_cast<f32>(packet.Height));
        m_Device->SetScissor(cmd, 0, 0, packet.Width, packet.Height);

        if (dc.Texture.IsValid())
            m_Device->BindTexture(cmd, dc.Texture, dc.Sampler);

        m_Device->Draw(cmd, dc.VertexCount);
    }

    m_Device->EndRendering(cmd);

    // --- Submit and present -------------------------------------------------
    m_Device->EndCommandList(cmd);
    m_Device->Submit({ cmd });
    m_Device->Present(packet.Swapchain);
}

} // namespace Arcbit
