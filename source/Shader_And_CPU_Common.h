#pragma once

#ifdef __cplusplus
#include "Math.hpp"
using namespace lib;
#endif

struct Constant_Data_Frame
{
	Vec4 light_pos;
	Vec4 light_col;
};

struct Constant_Data_Draw
{
	Vec3 albedo;
	f32 pad;
};