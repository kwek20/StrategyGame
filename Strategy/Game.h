#ifndef __GAME_H_INCLUDED__
#define __GAME_H_INCLUDED__

#pragma once

#include "ScreenState.h"
#include "ResourceManager.h"

class Game {
public:
	Game();

	void setManager(ResourceManager *manager);

	void setScreenState(ScreenState *state);
	void tick(float deltatime);

	void handleEvent(ALLEGRO_EVENT ev);

	ResourceManager *getManager();
	bool shouldShutDown();
	std::string getShutDownReason();
	void shutDown();

private:
	ScreenState *state;
	ResourceManager *manager;
};


#endif