#include "Map.h"


Map::Map(void){
	Generator *g = new FlagGenerator();
	terrain = new Terrain(TERRAIN_X, TERRAIN_Y, g);
}
 
Map::~Map(void){

}

void Map::draw(void){
	terrain->draw();
}
