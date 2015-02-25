#include "Resources.h"
ALLEGRO_FONT *Font::loadFile(const char *fileName){
	return al_load_ttf_font(fileName, 10, 0);
}

ALLEGRO_BITMAP *Image::loadFile(const char *fileName){
	return al_load_bitmap(fileName);
}

ALLEGRO_SAMPLE *Sound::loadFile(const char *fileName){
	return al_load_sample(fileName);
}

