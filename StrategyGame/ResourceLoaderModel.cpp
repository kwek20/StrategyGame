#include "ResourceLoaderModel.h"

#include "Model.h"

Model *ModelLoader::loadFile(const char *fileName){
	return new Model(fileName);
}