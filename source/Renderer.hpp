#pragma once

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