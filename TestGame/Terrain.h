#ifndef __TERRAIN_H_INCLUDED__
#define __TERRAIN_H_INCLUDED__
#pragma once

#include <allegro5\allegro.h>
#include <allegro5\allegro_opengl.h>

#include "Decorator.h"
#include "Generator.h"

#include "noiseutils.h"

#include <vector>


class Terrain {

public:

	Terrain(int x, int y, Generator *g, int angle = 10);
	virtual ~Terrain(void);

	/**
		Load the height map into an x,y,z coordinate system
	*/
	void load_ht_map(ALLEGRO_BITMAP* heightMap, std::vector<GLfloat> &verts, std::vector<GLbyte> &colors, GLfloat land_scale = 6.0f, GLfloat height_scale = 200.0f);

	/**
	   Generates the points for each triangle
	   These will be added according to this logic:
	    - push_back(i)
        - push_back(i + 1)
        - push_back(i + xSize)
	  */
	void make_point_connections(std::vector<GLuint> &points);

	void draw(void);

	void save(noise::utils::Image image, std::string name);

	GLfloat getTopHeight(){return max_height;} 
private:
	int xSize, ySize;
	GLfloat max_height;
	
	ALLEGRO_BITMAP* heightMap;
	std::vector<GLfloat> land_verticles;
	std::vector<GLuint> connect_points;
	std::vector<GLbyte> color_points;

	Decorator decorator;
};

#endif

