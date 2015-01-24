#ifndef __TERRAIN_H_INCLUDED__
#define __TERRAIN_H_INCLUDED__
#pragma once

#include <allegro5\allegro.h>
#include <allegro5\allegro_opengl.h>

#include "noiseutils.h"
#include <vector>


class Terrain {

public:

	Terrain(int x, int y, int angle = 10);
	virtual ~Terrain(void);
	ALLEGRO_BITMAP* generateHeightMap(void);

	/**
		
	*/
	void load_ht_map(ALLEGRO_BITMAP* heightMap, std::vector<GLfloat> &verts, GLfloat land_scale = 5.0f, GLfloat height_scale = 200.0f, GLfloat height_true = 0.0f);

	/**
	   Generates the points for each triangle
	   These will be added according to this logic:
	    - push_back(i)
        - push_back(i + 1)
        - push_back(i + xSize)
	  */
	void make_point_connections(std::vector<GLuint> &points);

	void draw(void);

	/*
		Converts Util bitmaps to Allegro bitmaps
	*/
	ALLEGRO_BITMAP* convertBMP(utils::Image image);

	/*
		Converts util COlors to Allegro colors
	*/
	ALLEGRO_COLOR convertColor(utils::Color color);

private:
	int xSize, ySize;
	float angle;
	GLuint agl_tex;
	
	std::vector<GLfloat> land_verticles;
	std::vector<GLuint> connect_points;

	void camera_2D_setup();
	void camera_3D_setup();
};

#endif

