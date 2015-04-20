#ifndef __SCREENSTATE_H_INCLUDED__
#define __SCREENSTATE_H_INCLUDED__

#pragma once

#include <iostream>

#include <allegro5\allegro.h>

class Game;

class ScreenState {
public:
	ScreenState(ALLEGRO_DISPLAY* display) { this->display = display; shutdown = false; }
	~ScreenState() {}

	virtual void end(Game *game);
	virtual void start(Game *game, ScreenState *previous);

	virtual void tick(double deltaTime) = 0;

	virtual void handleKeyboard(ALLEGRO_EVENT_TYPE type, int keycode) = 0;
	virtual void handleMouse(ALLEGRO_EVENT_TYPE type, ALLEGRO_MOUSE_STATE state) = 0;

	bool shouldShutDown();
	void shutDown();

	std::string getShutDownReason(void);
	void setShutDownReason(std::string reason);

	void log(std::string message);

	void error(std::string message);

	ALLEGRO_DISPLAY *getDisplay();
	int getDisplayWidth();
	int getDisplayHeight();
private:
	bool shutdown;
	std::string shutdownReason;

	ALLEGRO_DISPLAY *display;
};


#endif
