#include "Map.h"
#include "Player.h"

Map::Map(void){
	Generator *g = new FlagGenerator();
	terrain = new Terrain(TERRAIN_X, TERRAIN_Y, g);

	addEntity(new Player());
	dump();
}
 
Map::~Map(void){

}

void Map::draw(void){
	terrain->draw();
}

template <class T>
std::vector<T*> Map::getEntitiesByClass(){
	std::vector<T*> v;
	
	for (Entity *e : entities){
		T *ent = dynamic_cast<T*>(e);
		if (ent) v.push_back(ent);
	}

	return v;
}

void Map::dump(){
	std::cout << "-- Map dump --\n";
	if (objects.empty()){
		std::cout << "No objects\n";
	} else {
		std::cout << objects.size() << " objects: \n";
		for (Object o : objects){
			o.dump();
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
