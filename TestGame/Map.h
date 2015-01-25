#ifndef __MAP_H_INCLUDED__
#define __MAP_H_INCLUDED__

#pragma once

#include "Terrain.h"
#include "Object.h"

#include <vector>

#define TERRAIN_X 256
#define TERRAIN_Y 256

class Map
{
public:
	Map(void);
	~Map(void);

	void draw(void);
private:
	Terrain *terrain;
	std::vector<Object> objects;
};

#endif
