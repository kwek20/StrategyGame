#include "ResourceLoaderMesh.h"

#include "Mesh.h"

Mesh *MeshLoader::loadFile(const char *fileName){
	return new Mesh(fileName);
}

void MeshLoader::loadDataFile(int delSize, std::string path){
	ALLEGRO_PATH *start = al_get_standard_path(ALLEGRO_RESOURCES_PATH);
	ALLEGRO_PATH *p = al_create_path(path.c_str());

	const char* comp = al_get_path_component(p, al_get_path_num_components(start)+1);
	if (stricmp(al_get_path_extension(p)+1, comp) == 0){ //same extension as folder name?
		ResourceLoader::loadDataFile(delSize, path);
	}

	al_destroy_path(p);
}