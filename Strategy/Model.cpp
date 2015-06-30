#include "Model.h"

Model::Model(std::string sFilePath) : Object(){
	mesh = new Mesh(sFilePath.c_str());
}

Model::~Model(void){

}

void Model::draw(){
	glPushMatrix();
	glTranslatef(getXPos(), getYPos(), getZPos());
	mesh->render();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}