#ifndef __TERRAIN_H_INCLUDED__
#define __TERRAIN_H_INCLUDED__
#pragma once

#define WIDTH 512
#define HEIGHT 512

#include <allegro5\allegro.h>
#include "noiseutils.h"

class Terrain {

public:

	Terrain(int x = WIDTH, int y = WIDTH);
	virtual ~Terrain(void);
	void generateHeightMap(void);
	void draw(void);

	ALLEGRO_BITMAP* convertBMP(utils::Image image);
	ALLEGRO_COLOR convertColor(utils::Color color);

private:
	int xSize, ySize;
	ALLEGRO_BITMAP* heightMap;
};

#endif

