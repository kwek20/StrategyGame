#include "Terrain.h"
#include "Math.hpp"

#include <gl\glu.h>
#include <allegro5\opengl\gl_ext.h>

#include <iostream>

Terrain::Terrain(int x, int y, Generator *gen, int angle){
	xSize = x < 1 ? 1 : x;
	ySize = y < 1 ? 1 : y;

	std::cout << "Generating heightmap.\n";
	heightMap = gen->generate(x, y);
	
	if (!heightMap){
		std::cout << "error loading heightmap!\n";
		return;
	}

	std::cout << "Loading height map verticles..\n";
	load_ht_map(heightMap, land_verticles, color_points);

	std::cout << "Making connection points...\n";
	make_point_connections(connect_points);
}

Terrain::~Terrain(void){
	al_destroy_bitmap(heightMap);
}

void Terrain::draw(int mode){
	if (!heightMap) return;
	
	//select model stack
	//glMatrixMode(GL_MODELVIEW);
	glPolygonMode(GL_FRONT_AND_BACK, mode);

	//enable array for use during rendering
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	//give vertex info, 3 points(triangle), using floats, array position
	glColorPointer(3, GL_UNSIGNED_BYTE, 0, &color_points.at(0));
	glVertexPointer(3, GL_FLOAT, 0, &land_verticles.at(0));
	
	//draw elements, type triangle, array size, object type, array position,
	glDrawElements(GL_TRIANGLES, connect_points.size(), GL_UNSIGNED_INT, &connect_points.at(0));

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
}

void Terrain::save(noise::utils::Image image, std::string name){
	utils::WriterBMP writer;
	writer.SetSourceImage (image);
	writer.SetDestFilename (name);
	writer.WriteDestFile ();
}

//loads the height map into points on the terrain
void Terrain::load_ht_map(ALLEGRO_BITMAP* heightMap, std::vector<GLfloat> &verts, std::vector<GLbyte> &colors, GLfloat land_scale, GLfloat height_scale){
    ALLEGRO_COLOR ht_pixel;
	GLfloat height_true = 0.0f;
    int bmp_x = 0;
    int bmp_z = 0;
    unsigned char r, g, b;

	tile_size = land_scale;
 
    // locking the bitmap makes the reading of it noticably faster...
    al_lock_bitmap(heightMap, ALLEGRO_PIXEL_FORMAT_ANY, ALLEGRO_LOCK_READONLY);
 
    // autocentering variables
    GLfloat cent_wd = GLfloat((xSize) / 2.0f) * land_scale;
    GLfloat cent_ht = GLfloat((ySize) / 2.0f) * land_scale;
 
    verts.reserve(ySize * xSize * 3);
	colors.reserve(ySize * xSize * 3);
	//std::cout <<"landscale="<<land_scale<<" heightscale="<<height_scale<<"\n";
	//std::cout <<"cent_wd="<<cent_wd<<" cent_ht="<<cent_ht<<"\n";
	//for every x, z coord
	for(bmp_z = 0; bmp_z < ySize; bmp_z++){
		for(bmp_x = 0; bmp_x < xSize; bmp_x++){
			ht_pixel = al_get_pixel(heightMap, bmp_x, bmp_z); // get pixel color
			al_unmap_rgb(ht_pixel, &r, &g, &b);

			//add x
			verts.push_back(GLfloat((bmp_x * land_scale) - cent_wd));

			//add y, color to greyscale conversion
			height_true = GLfloat(float(0.299*r + 0.587*g + 0.114*b) / 255.0f) * height_scale;
			if (max_height < height_true) max_height = height_true;
			verts.push_back(height_true);

			//add z
			verts.push_back(GLfloat((bmp_z * land_scale) - cent_ht));

			decorator.getColorAt(height_true, &r, &g, &b);
			colors.push_back(r);
			colors.push_back(g);
			colors.push_back(b);

			//std::cout << "x="<<GLfloat((bmp_x * land_scale) - cent_wd)<<" y="<<height_true<<" z="<<GLfloat((bmp_z * land_scale) - cent_ht)<<" - r="<<r<<" g="<<g<<" b="<<b<<"\n";
			//al_rest(1);
		}
	}
    al_unlock_bitmap(heightMap);
}

//link the verticle points to 2 triangle abc's
void Terrain::make_point_connections(std::vector<GLuint> &points){
    GLuint i = 0;
    size_t point_count = (xSize - 1) * (ySize - 1) * 6;
    GLuint triangle_count = ((xSize - 1) * (ySize - 1)) + (ySize - 1);
    points.reserve(point_count);
 
    int width_cnt = 0;
 
    for(i = 0; i < triangle_count; i++){
        if(width_cnt == (xSize - 1)){
            width_cnt = 0;
            continue;
        } else {
            /*
				|\
				|_\
			*/
			// Triangle 1
            points.push_back(i);
            points.push_back(i + 1);
            points.push_back(i + xSize);
 
			/*
				\--|
				 \ |
			*/
            // Triangle 2
            points.push_back(i + 1);
            points.push_back(i + xSize + 1);
            points.push_back(i + xSize);

            // Mod: both triangles go around counter clockwise now...
            width_cnt++;
        }
    }
}

//returns max ySize if not possible
float Terrain::getHeight(float x, float z){

	Vec3<GLfloat> p1, p2, p3;
	p1 = getPoint(x,z);
	p2 = getPoint(x-tile_size,z);
	p3 = getPoint(x,z-tile_size);
	float height = calcY(p1, p2, p3, x, z);
	return height;
}

Vec3<GLfloat> Terrain::getPoint(float x, float z){
	x = x - fmod(x, tile_size);
	z = z - fmod(z, tile_size);

	x/=tile_size;
	z/=tile_size;

	//vec3 center to vec2 center
	x+= xSize/2;
	z+= ySize/2;
	if (x >= xSize || z  >= ySize) return Vec3<GLfloat>(0,0,0);

	//amount per coordinate * z position * amount per z + x 
	int position = 3 * (z*xSize + x);
	return Vec3<GLfloat>(land_verticles.at(position), land_verticles.at(position+1), land_verticles.at(position+2));
}
