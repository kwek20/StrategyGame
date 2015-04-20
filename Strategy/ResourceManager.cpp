#include "ResourceManager.h"

ResourceManager::ResourceManager(void){
	sounds = new SoundLoader(SOUNDS);
	fonts = new FontLoader(FONTS);
	images = new ImageLoader(IMAGES);
	models = new ModelLoader(MODELS);

	sounds->load();
	fonts->load();
	images->load();

	models->load();
	//Model::FinalizeVBO();
} 

ResourceManager::~ResourceManager(void){
	delete sounds;
	delete fonts;
	delete images;
	delete models;
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

Model* ResourceManager::getModel(std::string name){
	return models->getData(name);
}
