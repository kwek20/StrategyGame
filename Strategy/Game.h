#ifndef __GAME_H_INCLUDED__
#define __GAME_H_INCLUDED__

#pragma once

#include "ScreenState.h"

class Game {
	void setScreenState(ScreenState *state);
	bool tick(float deltatime);
};

#endif