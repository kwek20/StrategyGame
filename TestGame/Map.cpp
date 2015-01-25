#include "Map.h"


Map::Map(void){
	terrain = new Terrain(TERRAIN_X, TERRAIN_Y);
}


Map::~Map(void)
{
}

void Map::draw(void){
	terrain->draw();
}
