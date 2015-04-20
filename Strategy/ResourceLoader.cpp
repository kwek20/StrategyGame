#include "ResourceLoader.h"

ALLEGRO_FONT *FontLoader::loadFile(const char *fileName){
	return al_load_ttf_font(fileName, 10, 0);
}

ALLEGRO_BITMAP *ImageLoader::loadFile(const char *fileName){
	return al_load_bitmap(fileName);
}

ALLEGRO_SAMPLE *SoundLoader::loadFile(const char *fileName){
	return al_load_sample(fileName);
}

