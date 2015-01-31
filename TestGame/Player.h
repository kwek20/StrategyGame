#ifndef __PLAYER_H_INCLUDED__
#define __PLAYER_H_INCLUDED__

#pragma once

#include "LivingEntity.h"

class Player : public Entity
{
public:
	virtual int getMaxPitch(){return 15;}
	virtual int getMinPitch(){return 75;}
	virtual int getMaxYaw(){return 360;}
	virtual int getMinYaw(){return 0;}

	Player(double x, double y, double z) : Entity(x,y,z, (getMaxPitch()+getMinPitch())/2, 0, 0){}

	void moveAdd(Vec3<double> locTo);

	const std::string getName(){return "Player";}
};

#endif