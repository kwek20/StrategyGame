#include "ResourceManager.h"

ResourceManager::ResourceManager(void){
	sounds = new Sound(SOUNDS);
	fonts = new Font(FONTS);
	images = new Image(IMAGES);

	sounds->load();
	fonts->load();
	images->load();
} 

ResourceManager::~ResourceManager(void){
	delete sounds;
	delete fonts;
	delete images;
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
