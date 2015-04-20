#ifndef __LOADSTATE_H_INCLUDED__
#define __LOADSTATE_H_INCLUDED__

#pragma once

#include "ScreenState.h"
#include "Game.h"

class LoadState :
	public ScreenState
{
public:
	LoadState(ALLEGRO_DISPLAY* display);
	~LoadState();

	void tick(double deltaTime);

	void handleKeyboard(ALLEGRO_EVENT_TYPE type, int keycode);
	void handleMouse(ALLEGRO_EVENT_TYPE type, ALLEGRO_MOUSE_STATE state);

	void start(Game *game, ScreenState *previous);
	void end(Game *game);

	void loadManager(Game *game);

private:
	bool loaded;
	std::string currentMessage;
};

#endif
