#ifndef __GAME_H_INCLUDED__
#define __GAME_H_INCLUDED__

#pragma once

#include <string>
#include <allegro5\allegro.h>

#include "Map.h"
#include "Camera.h"
#include "HUD.h"
#include "ResourceManager.h"

class Game
{
private:
	ALLEGRO_DISPLAY *display;
	Map *map;
	Camera *camera;
	HUD *hud;
	ResourceManager *manager;

	bool shutdown, pause;
	std::string shutdownReason;

	float mouseTempX, mouseTempY;

public:
	Game(ALLEGRO_DISPLAY* display);

	virtual ~Game(void);

	void tick(double deltaTime);
	void togglePause(){pause = !pause;}

	bool shouldShutDown(){return shutdown;}
	void shutDown(){shutdown=true;}

	std::string getShutDownReason(void){return shutdownReason;}
	void setShutDownReason(std::string reason){shutdownReason = reason;}

	void log(std::string message){fprintf(stdout, message.c_str());}

	void error(std::string message){fprintf(stderr, message.c_str());}

	void handleKeyboard(ALLEGRO_EVENT_TYPE type, int keycode);
	void handleMouse(ALLEGRO_EVENT_TYPE type, ALLEGRO_MOUSE_STATE state);

	Map *getMap(){return map;}
	Camera *getCamera(){return camera;}
	HUD *getHud(){return hud;}
	ResourceManager *getManager(){return manager;}
};

#endif
