#include "Object.h"

Object::Object(void){
	load(Vec3<double>(0, 0, 0), Vec3<double>(0, 0, 0));
}

Object::Object(double x, double y, double z){
	load(Vec3<double>(x, y, z), Vec3<double>(0, 0, 0));
}

Object::Object(double x, double y, double z, double xr, double yr, double zr){
	load(Vec3<double>(x, y, z), Vec3<double>(xr, yr, zr));
}

Object::Object(Vec3<double> position){
	load(position, Vec3<double>(0, 0, 0));
}

Object::Object(Vec3<double> position, Vec3<double> rotation){
	load(position, rotation);
}

void Object::moveAdd(Vec3<double> newPos){
	moveTo(position + newPos);
}

void Object::moveTo(Vec3<double> newPos){
	position = newPos;
}

void Object::rotateAdd(Vec3<double> newRot){
	rotateTo(rotation + newRot);
}

void Object::rotateTo(Vec3<double> newRot){
	rotation = newRot;
}

void Object::load(Vec3<double> position, Vec3<double> rotation){
	// Set position, rotation 

	this->position = position;
	this->rotation = rotation;
}

Object::~Object(void){

}

void Object::dump(){
	std::cout << getName().c_str() << ": position=[" << position << "] " << "rotation=[" << rotation << "]\n";
}
