#pragma once

#include <arcbit/core/Types.h>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Handle<Tag>
//
// A typed, generational index into a backend resource pool.
// Stored as a single u32: 20 bits for index, 12 bits for generation.
//
//   Index 0 is always invalid — pool slot 0 is never assigned.
//   Generation is bumped on free so stale handles can be detected.
//
// Handles are trivially copyable and comparable as plain integers.
// ---------------------------------------------------------------------------
template<typename Tag>
struct Handle
{
    u32 Value = 0;

    static constexpr u32 IndexBits = 20;
    static constexpr u32 GenBits   = 12;
    static constexpr u32 IndexMask = (1u << IndexBits) - 1u;
    static constexpr u32 GenMask   = (1u << GenBits)   - 1u;
    static constexpr u32 GenShift  = IndexBits;

    [[nodiscard]] u32  Index()      const noexcept { return  Value & IndexMask; }
    [[nodiscard]] u32  Generation() const noexcept { return (Value >> GenShift) & GenMask; }
    [[nodiscard]] bool IsValid()    const noexcept { return  Index() != 0; }

    // Called by the backend pool — not by game code.
    static Handle Make(u32 index, u32 generation) noexcept
    {
        return Handle{ ((generation & GenMask) << GenShift) | (index & IndexMask) };
    }

    static constexpr Handle Invalid() noexcept { return Handle{ 0 }; }

    bool operator==(const Handle& o) const noexcept { return Value == o.Value; }
    bool operator!=(const Handle& o) const noexcept { return Value != o.Value; }
};

static_assert(sizeof(Handle<struct DummyTag>) == sizeof(u32));

// ---------------------------------------------------------------------------
// Concrete handle types
// ---------------------------------------------------------------------------
struct BufferTag      {};
struct TextureTag     {};
struct ShaderTag      {};
struct PipelineTag    {};
struct SwapchainTag   {};
struct CommandListTag {};
struct SamplerTag     {};

using BufferHandle      = Handle<BufferTag>;
using TextureHandle     = Handle<TextureTag>;
using ShaderHandle      = Handle<ShaderTag>;
using PipelineHandle    = Handle<PipelineTag>;
using SwapchainHandle   = Handle<SwapchainTag>;
using CommandListHandle = Handle<CommandListTag>;
using SamplerHandle     = Handle<SamplerTag>;

} // namespace Arcbit
