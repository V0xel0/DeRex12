#include <cassert>

#include "Utils.hpp"
#include "Allocators.hpp"
#include "GameAsserts.hpp"
#include "Views.hpp"
#include "Math.hpp"

#include "Game_Services.hpp"
#include "Renderer.hpp"

#include "DX_Managment.hpp"

extern "C" RENDERER_FULL_UPDATE(renderer_full_update)
{
	Renderer_State* renderer_state = (Renderer_State*)memory->permanent_storage;
	if (!memory->is_initalized)
	{
		renderer_state->arena_logic.max_size = memory->size_permanent_storage - sizeof(Renderer_State);
		renderer_state->arena_logic.base = (byte*)memory->permanent_storage + sizeof(Renderer_State);
		renderer_state->arena_asssets.max_size = memory->size_transient_storage;
		renderer_state->arena_asssets.base = (byte*)memory->transient_storage;
		
		//Static data init
		Array_View<Vertex>vertex_data{};
		vertex_data.init(&renderer_state->arena_asssets, 
		                 Vertex{ {	-0.5f, 0.5f, 0.5f, 1.0f },	{ 1.0f, 0.0f, 0.0f, 1.0f } },
										 Vertex{ {	0.5f, -0.5f, 0.5f, 1.0f },	{ 0.0f, 1.0f, 0.0f, 1.0f } },
										 Vertex{ { -0.5f, -0.5f, 0.5f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.5f,  0.5f, 0.5f, 1.0f }, 	{ 0.0f, 1.0f, 1.0f, 1.0f } },
		
										 Vertex{ { -0.75f, 0.75f,	0.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.0f,	 0.0f,	0.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ { -0.75f, 0.0f,	0.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.0f,  0.75f,	0.7f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } });
		
		Array_View<u16>indices_data{};
		indices_data.init(&renderer_state->arena_asssets, 0, 1, 2,  0, 3, 1);
		
		DX::init_d3d12(window->handle, window->width, window->height);
		DX::create_basic_pipeline();
		DX::init_static_data(vertex_data, indices_data);
		
		memory->is_initalized = true;
	}
	
	DX::render_frame(window->width, window->height);
}