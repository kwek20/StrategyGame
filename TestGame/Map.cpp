#include "Map.h"
#include "Player.h"

Map::Map(void){
	Generator *g = new FlagGenerator();
	terrain = new Terrain(TERRAIN_X, TERRAIN_Y, g);

	addEntity(new Player(0,terrain->getTopHeight()*1.5,0));
}
 
Map::~Map(void){

}

void Map::draw(void){
	terrain->draw();
	for (Entity *e : entities){e->draw();}
	for (Object *o : objects){o->draw();}
}

float Map::getHeightAt(float x, float z){
	//for (int point=0; point < terrain->connect_points.size()){

	//}

	return 0;
}

void Map::dump(){
	std::cout << "-- Map dump --\n";
	if (objects.empty()){
		std::cout << "No objects\n";
	} else {
		std::cout << objects.size() << " objects: \n";
		for (Object *o : objects){
			o->dump();
		}
	}

	if (entities.empty()){
		std::cout << "No Entities\n";
	} else {
		std::cout << entities.size() << " entities: \n";
		for (Entity *e : entities){
			e->dump();
		}
	}
	std::cout << "-- end dump --\n";
}
