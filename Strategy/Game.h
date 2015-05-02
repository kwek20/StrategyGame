#ifndef __GAME_H_INCLUDED__
#define __GAME_H_INCLUDED__

#pragma once

#include "ScreenState.h"
#include "ResourceManager.h"
#include "Scheduler.h"

class Game {
public:
	Game();

	void setManager(ResourceManager *manager);

	void setScreenState(ScreenState *state);
	void tick(float deltatime);

	void handleEvent(ALLEGRO_EVENT ev);

	ResourceManager *getManager();
	Scheduler *getScheduler();

	bool shouldShutDown();
	std::string getShutDownReason();
	void shutDown();

	static Game *get(){ return game; }
private:
	ScreenState *state;
	ResourceManager *manager;
	Scheduler *scheduler;

	int ticks = 0;

	static Game *game;
};

#endif