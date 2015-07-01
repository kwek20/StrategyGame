#include "Model.h"

Model::Model(Mesh *mesh, Vec3<double> position, Vec3<double> rotation){
	load(position, rotation);
	this->mesh = mesh;
}

Model::~Model(void){

}

void Model::draw(){
	glTranslatef(getXPos(), getYPos(), getZPos());
	mesh->render();
	//glMatrixMode(GL_MODELVIEW);
	//glLoadIdentity();
}