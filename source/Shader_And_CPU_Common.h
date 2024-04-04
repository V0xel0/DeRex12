#pragma once

#ifdef __cplusplus
#include "Math.hpp"
using namespace lib;
#endif

struct Constant_Data_Frame
{
	Vec4 light_pos;
	Vec4 light_col;
	Vec4 view_pos;
};

struct Constant_Data_Draw
{
	Mat4 obj_to_world;
	Mat4 world_to_clip;
	Vec3 albedo;
	f32 pad;
};