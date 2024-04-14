#pragma once

struct Game_Key_State
{
	s32 halfTransCount;
	b32 wasDown;
};

struct Game_Mouse_Data
{
	s32 x;
	s32 y;
	s32 delta_x;
	s32 delta_y;
	s32 delta_wheel;
};

struct Game_Controller
{
	b16 isConnected;
	Game_Mouse_Data mouse;

	union
	{
		Game_Key_State buttons[12];
		struct
		{
			Game_Key_State move_forward;
			Game_Key_State move_backward;
			Game_Key_State move_left;
			Game_Key_State move_right;

			Game_Key_State main_click;
			Game_Key_State action1;
			Game_Key_State action2;
			Game_Key_State action3;
			Game_Key_State action4;
			
			Game_Key_State rotate_start;

			Game_Key_State back;
			Game_Key_State start;

			// All buttons must be added above this line
			Game_Key_State terminator;
		};
	};
};

struct Game_Input
{
	Game_Controller controllers[2];
};

struct Game_Memory
{
	b32 is_initalized;
	
	u64 size_permanent_storage;
	void *permanent_storage;

	u64 size_transient_storage;
	void *transient_storage;
};

#if GAME_INTERNAL
struct Debug_File_Output
{
	void *data;
	u32 dataSize;
};
#endif

inline Game_Controller *get_game_controller(Game_Input *input, u32 controllerID)
{
	GameAssert(controllerID < (u32)array_count_64(input->controllers));
	return &input->controllers[controllerID];
}

struct Data_To_RHI;
struct Game_Window;

extern "C" Data_To_RHI* app_full_update(Game_Memory *memory, Game_Window *window, Game_Input *inputs);
using app_update_ptr = decltype(app_full_update);
