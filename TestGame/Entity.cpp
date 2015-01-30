#include "Entity.h"


Entity::Entity(void){
	load(Vec3<double>(0,0,0), Vec3<double>(0,0,0));
}

Entity::Entity(Vec3<double> position){
	load(position, Vec3<double>(0,0,0));
}

Entity::Entity(Vec3<double> position, Vec3<double> rotation){
	load(position, rotation);
}

Entity::~Entity(void){

}

void Entity::unPossess(){

}

void Entity::load(Vec3<double> position, Vec3<double> rotation){
// Set position, rotation and speed values to zero
	position.zero();
	rotation.zero();
 
	// How fast we move (higher values mean we move and strafe faster)
	movementSpeedFactor = 100.0;
}