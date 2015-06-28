#include "Model.h"

Model::Model(std::string sFilePath) : Object(){
	mesh = new Mesh(sFilePath.c_str());
}

void Model::draw(){
	glPushMatrix();
	glTranslatef(50, 50, 50);
	mesh->render();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}