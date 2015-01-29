#ifndef __HUD_H_INCLUDED__
#define __HUD_H_INCLUDED__

#pragma once

class Game;

class HUD abstract
{
public:
	HUD(void);
	~HUD(void);

	virtual void draw(Game *game) = 0;
};

class IngameHUD : public HUD
{
public:
	IngameHUD(void) : HUD(){}
	~IngameHUD(void);
	void draw(Game *game);
};

#endif