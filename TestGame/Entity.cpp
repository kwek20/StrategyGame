#include "Entity.h"
#include <typeinfo>
#include <stdio.h>

Entity::Entity(void){
	load(Vec3<double>(0,0,0), Vec3<double>(0,0,0));
}

Entity::Entity(double x, double y, double z){
	load(Vec3<double>(x,y,z), Vec3<double>(0,0,0));
}

Entity::Entity(double x, double y, double z, double xr, double yr, double zr){
	load(Vec3<double>(x,y,z), Vec3<double>(xr,yr,zr));
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

void Entity::moveAdd(Vec3<double> newPos){
	moveTo(position + newPos);
}

void Entity::moveTo(Vec3<double> newPos){
	position = newPos;
}

void Entity::rotateAdd(Vec3<double> newRot){
	rotateTo(rotation + newRot);
}

void Entity::rotateTo(Vec3<double> newRot){
	rotation = newRot;
}

void Entity::load(Vec3<double> position, Vec3<double> rotation){
	// Set position, rotation 

	this->position = position;
	this->rotation = rotation;

	// How fast we move (higher values mean we move and strafe faster)
	movementSpeedFactor = 100.0;
}

void Entity::draw(){

}

void Entity::dump(){
	std::cout << getName().c_str() << ": position=[" << position << "] " << "rotation=[" << rotation << "]\n";
}