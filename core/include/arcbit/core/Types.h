#pragma once

#include <cstdint>
#include <cstddef>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Signed integers
// ---------------------------------------------------------------------------
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

// ---------------------------------------------------------------------------
// Unsigned integers
// ---------------------------------------------------------------------------
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

// ---------------------------------------------------------------------------
// Floating point
// ---------------------------------------------------------------------------
using f32 = float;
using f64 = double;

// ---------------------------------------------------------------------------
// Pointer-sized types
// ---------------------------------------------------------------------------
using usize = size_t;      // unsigned — use for sizes, counts, indices
using isize = ptrdiff_t;   // signed   — use for pointer differences, offsets

// ---------------------------------------------------------------------------
// Byte — raw memory, not a character
// ---------------------------------------------------------------------------
using byte = u8;

// ---------------------------------------------------------------------------
// Compile-time size guarantees.
// If any of these fire, the platform/compiler assumptions are wrong.
// ---------------------------------------------------------------------------
static_assert(sizeof(i8)  == 1);
static_assert(sizeof(i16) == 2);
static_assert(sizeof(i32) == 4);
static_assert(sizeof(i64) == 8);

static_assert(sizeof(u8)  == 1);
static_assert(sizeof(u16) == 2);
static_assert(sizeof(u32) == 4);
static_assert(sizeof(u64) == 8);

static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);

static_assert(sizeof(usize) == sizeof(void*));
static_assert(sizeof(isize) == sizeof(void*));

} // namespace Arcbit
