#include "Utils.hpp"
#include "Allocators.hpp"
#include "GameAsserts.hpp"
#include "Game_Services.hpp"
#include "Renderer.hpp"

extern "C" RENDERER_FULL_UPDATE(renderer_full_update)
{
	Renderer_State* renderer_state = (Renderer_State*)memory->permanent_storage;
	if (!memory->is_initalized)
	{
		renderer_state->arena_logic.max_size = memory->size_permanent_storage - sizeof(Renderer_State);
		renderer_state->arena_logic.base = (byte*)memory->permanent_storage + sizeof(Renderer_State);
		renderer_state->arena_asssets.max_size = memory->size_transient_storage;
		renderer_state->arena_asssets.base = (byte*)memory->transient_storage;
		
		memory->is_initalized = true;
	}
}