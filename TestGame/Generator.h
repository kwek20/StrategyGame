#ifndef __GENERATOR_H_INCLUDED__
#define __GENERATOR_H_INCLUDED__

#pragma once

#include <allegro5\allegro.h>

#include "noiseutils.h"

class Generator abstract
{
public:
	Generator(void);
	virtual ~Generator(void);
	virtual ALLEGRO_BITMAP* generate(int xSize, int ySize) = 0;

	/*
		Converts Util bitmaps to Allegro bitmaps
	*/
	ALLEGRO_BITMAP* convertBMP(utils::Image image);

	/*
		Converts util COlors to Allegro colors
	*/
	ALLEGRO_COLOR convertColor(utils::Color color);
};

class FlagGenerator : public Generator
{
	ALLEGRO_BITMAP* generate(int xSize, int ySize);
};

#endif