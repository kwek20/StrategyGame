#ifndef __HUD_H_INCLUDED__
#define __HUD_H_INCLUDED__

#pragma once
class HUD abstract
{
public:
	HUD(void);
	~HUD(void);

	virtual void draw() = 0;
};

class IngameHUD : public HUD
{
public:
	IngameHUD(void) : HUD(){}
	~IngameHUD(void);
	void draw();
};

#endif