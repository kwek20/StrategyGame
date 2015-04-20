#include "Model.h"

Model::Model(std::string sFilePath){
	mesh = new Mesh();
	mesh->LoadMesh(sFilePath);
}

void Model::draw(){
	mesh->Render();
}