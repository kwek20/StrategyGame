#include "Map.h"
#include "Player.h"

#include "Villager.h"
#include "Model.h"

Map::Map(ResourceManager *manager){
	Generator *g = new FlagGenerator();
	terrain = new Terrain(TERRAIN_X, TERRAIN_Y, g);

	addEntity(new Player(0,terrain->getTopHeight()*1.5,0));
	addEntity(new Villager(5, terrain->getTopHeight()*1.5,-105));

	//addObject(manager->getModel("house"));
}
 
Map::~Map(void){

}

void Map::draw(float deltaTime){
	terrain->draw(); 
	for (Entity *e : entities){
		if (dynamic_cast<LivingEntity*>(e)){
			e->setInAir(getHeightAt(e->getXPos(), e->getZPos()) < e->getYPos());
		}

		e->update(deltaTime); 
		e->draw();
	}

	for (Object *o : objects){o->draw();}
}

float Map::getHeightAt(float x, float z){
	return terrain->getHeight(x,z);
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
