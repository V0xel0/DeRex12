#pragma once
#include <cassert>
#include "Utils.hpp"
#include "Math.hpp"
#include "GameAsserts.hpp"
#include "Game_Services.hpp"

struct Vertex
{
	lib::Vec4 position;
	lib::Vec4 color;
};

struct Renderer_State
{
	Alloc_Arena arena_logic;
	Alloc_Arena arena_asssets;
};