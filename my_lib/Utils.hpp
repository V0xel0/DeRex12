#pragma once
#include <cstdint>

using f32 = float;
using f64 = double;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using b8 = s8;
using b16 = s16;
using b32 = s32;

using byte = u8;

#define SoftAssert(cond) do { if (!(cond)) __debugbreak(); } while (0)

#define KiB(Value) ((Value)*1024LL)
#define MiB(Value) (KiB(Value) * 1024LL)
#define GiB(Value) (MiB(Value) * 1024LL)
#define TiB(Value) (GiB(Value) * 1024LL)

#define internal static
#define local_persist static
#define global_variable static

constexpr f32 PI32 = 3.14159265359f;
constexpr f64 PI64 = 3.14159265359;

#define AlignAddressPow2(Value, Alignment) ((Value) + ((Alignment)-1)) & ~((Alignment)-1)
#define AlignAddress4(Value) ((Value + 3) & ~3)
#define AlignAddress8(Value) ((Value + 7) & ~7)
#define AlignAddress16(Value) ((Value + 15) & ~15)

#define AlignValuePow2(Value, Alignment) ((Value) + ((Alignment)-1)) & -(Alignment)

#define TestBitPos(El, Pos) ((El) & (1 << (Pos))) // returns position or 0 if not set
#define TestBit(El,Pos) (((El) >> (Pos)) & 1) // return 0/1 if notset/set

template <typename F>
struct DummyDefer
{
	F f;
	DummyDefer(F f) : f(f) {}
	~DummyDefer() { f(); }
};

template <typename F>
DummyDefer<F> defer(F f)
{
	return DummyDefer<F>(f);
}

constexpr u32 trunc_u64_to_u32(u64 val)
{
	SoftAssert(val <= 0xffffffff);
	u32 Result = (u32)val;
	return (Result);
};

template <typename T, u64 N>
constexpr u64 array_count_64(const T (&array)[N]) noexcept
{
	return N;
}

template <typename T, u32 N>
constexpr u32 array_count_32(const T (&array)[N]) noexcept
{
	return N;
}

template <typename T>
constexpr void swap(T &a, T &b)
{
    T temp(static_cast<T&&>(a));
    a = static_cast<T&&>(b);
    b = static_cast<T&&>(temp);
}

template<typename T>
constexpr void pointer_swap(T **a, T **b)
{
    T *temp = *a;
    *a = *b;
    *b = temp;
}

template<typename T>
constexpr void value_swap(T *a, T *b)
{
    T temp = *a;
    *a = *b;
    *b = temp;
}

template<typename T>
constexpr void bit_swap(T *a, T *b)
{
	a = a ^ b;
	b = a ^ b;
	a = a ^ b;
}

template <typename ...T>
constexpr auto min_v(T&&... args)
{
	auto min = (args, ...);
	((args < min ? min = args : 0), ...);
    return min;
}

template <typename ...T>
constexpr auto max_v(T&&... args)
{
	auto max = (args, ...);
	((args > max ? max = args : 0), ...);
    return max;
}

template <typename T>
constexpr T clamp_v(T x, T low, T high)
{
	return min(max(low, x), high);
}
