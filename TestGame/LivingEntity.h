#pragma once
#include "entity.h"
class LivingEntity : public Entity
{
public:
	virtual int getMaxPitch(){return -75;}
	virtual int getMinPitch(){return 75;}
	virtual int getMaxYaw(){return 360;}
	virtual int getMinYaw(){return 0;}

	LivingEntity(void) : Entity(){}
	LivingEntity(double x, double y, double z) : Entity(x,y,z){}
	LivingEntity(double x, double y, double z, double xr, double yr, double zr) : Entity(x,y,z,xr,yr,zr){}

	~LivingEntity(void){};
};

