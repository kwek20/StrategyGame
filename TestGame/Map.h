#ifndef __MAP_H_INCLUDED__
#define __MAP_H_INCLUDED__

#pragma once

#include "Terrain.h"
#include "Object.h"

#include "Player.h"
#include "Entity.h"

#include <stdio.h>
#include <vector>

#define TERRAIN_X 256
#define TERRAIN_Y 256

class Map
{
private:
	Terrain *terrain;

	std::vector<Object*> objects;
	std::vector<Entity*> entities;

public:
	Map(void);
	~Map(void);

	void draw(void);
	float getX(){return TERRAIN_X;}
	float getZ(){return TERRAIN_Y;}
	float getHeight(){return terrain->getTopHeight();}

	void addEntity(Entity *entity){entities.push_back(entity);}

	float getHeightAt(float x, float z);
	
	template <typename T>
	std::vector<T*> getEntitiesByClass(){
		std::vector<T*> v;

		for (Entity *e : entities){
			T *ent = dynamic_cast<T*>(e);
			if (ent) v.push_back(ent);
		}

		return v;
	}

	void dump();
};

#endif
