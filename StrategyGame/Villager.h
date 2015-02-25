#pragma once

#include "ControllableEntity.h"

class Villager : public ControllableEntity
{
public:
	Villager(void);
	~Villager(void);

	virtual const std::string getName(){return "Villager";};

	Villager(float x, float y, float z) : ControllableEntity(x,y,z){}

};

