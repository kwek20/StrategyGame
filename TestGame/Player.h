#ifndef __PLAYER_H_INCLUDED__
#define __PLAYER_H_INCLUDED__

#pragma once

#include "entity.h"

class Player :
	public Entity
{
public:
	Player(void);
	virtual ~Player(void);
};

#endif