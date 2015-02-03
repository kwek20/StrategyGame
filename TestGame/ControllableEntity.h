#pragma once
#include "livingentity.h"
class ControllableEntity :
	public LivingEntity
{
public:
	ControllableEntity(void);
	~ControllableEntity(void);

	ControllableEntity(float x, float y, float z) : LivingEntity(x,y,z){}
};

