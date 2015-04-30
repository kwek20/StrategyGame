#ifndef __PLAYSTATE_H_INCLUDED__
#define __PLAYSTATE_H_INCLUDED__

#pragma once

#include "windows.h"

#include "ScreenState.h"

#include <string>
#include <allegro5\allegro.h>

#include "Map.h"
#include "Camera.h"
#include "HUD.h"
#include "ResourceManager.h"
#include "PlayerController.h"

class PlayState : public ScreenState
{
	Map *map;
	Camera *camera;
	HUD *hud;
	PlayerController *controller;

	bool pause;
	float mouseTempX = 0, mouseTempY = 0;
public:
	PlayState(ALLEGRO_DISPLAY* display);

	virtual ~PlayState(void);

	void tick(double deltaTime);
	void togglePause(){pause = !pause;}

	void handleKeyboard(ALLEGRO_EVENT_TYPE type, int keycode);
	void handleMouse(ALLEGRO_EVENT_TYPE type, ALLEGRO_MOUSE_STATE state);

	Map *getMap(){return map;}
	Camera *getCamera(){return camera;}
	HUD *getHud(){return hud;}
	PlayerController *getController(){return controller;}
};

#endif
