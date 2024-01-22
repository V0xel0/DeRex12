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
	s32 deltaX;
	s32 deltaY;
	s32 deltaWheel;
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
			Game_Key_State moveUp;
			Game_Key_State moveDown;
			Game_Key_State moveLeft;
			Game_Key_State moveRight;

			Game_Key_State actionFire;
			Game_Key_State action1;
			Game_Key_State action2;
			Game_Key_State action3;
			Game_Key_State action4;
			Game_Key_State action5;

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