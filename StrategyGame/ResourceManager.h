#ifndef __RESOURCE_MANAGER_H_INCLUDED__
#define __RESOURCE_MANAGER_H_INCLUDED__

#pragma once

#define SOUNDS "sounds"
#define FONTS "fonts"
#define IMAGES "images"
#define MODELS "models"

#include "Resources.h"
#include "Model.h"

#include "assimp/scene.h"

class ResourceManager
{
public:
	ResourceManager(void);
	~ResourceManager(void);

	ALLEGRO_BITMAP *getImage(std::string name);
	ALLEGRO_SAMPLE *getSound(std::string name);
	ALLEGRO_FONT *getFont(std::string name);
	const aiScene *getModel(std::string name);
private:
	Sound *sounds;
	Font *fonts;
	Image *images;
	Model *models;
};

#endif
