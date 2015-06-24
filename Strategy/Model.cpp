#include "Model.h"

Model::Model(std::string sFilePath){
	mesh = new Mesh(sFilePath.c_str());
}

void Model::draw(){
	mesh->render();
}