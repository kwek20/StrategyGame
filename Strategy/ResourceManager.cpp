#include "ResourceManager.h"

ResourceManager::ResourceManager(void){
	sounds = new SoundLoader(SOUNDS);
	fonts = new FontLoader(FONTS);
	images = new ImageLoader(IMAGES);
	meshes = new MeshLoader(MODELS);

	sounds->load();
	fonts->load();
	images->load();

	meshes->load();
} 

ResourceManager::~ResourceManager(void){
	delete sounds;
	delete fonts;
	delete images;
	delete meshes;
}

ALLEGRO_BITMAP* ResourceManager::getImage(std::string name){
	return images->getData(name);
}

ALLEGRO_SAMPLE* ResourceManager::getSound(std::string name){
	return sounds->getData(name);
}

ALLEGRO_FONT* ResourceManager::getFont(std::string name){
	return fonts->getData(name);
}

Mesh* ResourceManager::getMesh(std::string name){
	return meshes->getData(name);
}
