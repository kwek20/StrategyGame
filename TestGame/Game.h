#ifndef __GAME_H_INCLUDED__
#define __GAME_H_INCLUDED__

#pragma once

#include <string>
#include <allegro5\allegro.h>

#include "Terrain.h"
#include "noiseutils.h"

#define WIDTH 512
#define HEIGHT 512

class Game
{
public:

	Game(ALLEGRO_DISPLAY* display);

	virtual ~Game(void);

	void tick(void);

	bool shouldShutDown(){return shutdown;}
	void shutDown(){shutdown=true;}

	std::string getShutDownReason(void){return shutdownReason;}
	void setShutDownReason(std::string reason){shutdownReason = reason;}

	void log(std::string message){fprintf(stdout, message.c_str());}

	void error(std::string message){fprintf(stderr, message.c_str());}

private:
	ALLEGRO_DISPLAY *display;
	Terrain* terrain;

	bool shutdown;
	int num;
	std::string shutdownReason;
};

#endif
