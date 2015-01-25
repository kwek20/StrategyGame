#include "Terrain.h"
#include "noiseutils.h"
#include <gl\glu.h>

#include <allegro5\opengl\gl_ext.h>

#include <vector>
#include <iostream>

Terrain::Terrain(int x, int y, int angle){
	xSize = x < 1 ? 1 : x;
	ySize = y < 1 ? 1 : y;
	this->angle = angle < -360 ? 360 : (angle > 360 ? 360 : angle);

	std::cout << "Generating heightmap.\n";
	heightMap = generateHeightMap();

	std::cout << "Loading height map verticles..\n";
	load_ht_map(heightMap, land_verticles, color_points);

	std::cout << "Making connection points...\n";
	make_point_connections(connect_points);
}

Terrain::~Terrain(void){
	al_destroy_bitmap(heightMap);
}

void Terrain::draw(void){
	camera_3D_setup();

	//select model stack
	glMatrixMode(GL_MODELVIEW);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	//reset  matrix
	glLoadIdentity();
	
	//rotate to angle
	glRotatef(angle, 0.0f, 1.0f, 0.0f);

	//enable array for use during rendering
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	//give vertex info, 3 points(triangle), using floats, array position
	glColorPointer(3, GL_UNSIGNED_BYTE, 0, &color_points.at(0));
	glVertexPointer(3, GL_FLOAT, 0, &land_verticles.at(0));
	

	//draw elements, type triangle, array size, object type, array position,
	glDrawElements(GL_TRIANGLES, connect_points.size(), GL_UNSIGNED_INT, &connect_points.at(0));

	angle += 1;
	if (angle >= 360) angle = -360;

	camera_2D_setup();
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	al_draw_bitmap(heightMap, 0, 0, 0);
}

void Terrain::save(noise::utils::Image image, std::string name){
	utils::WriterBMP writer;
	writer.SetSourceImage (image);
	writer.SetDestFilename (name);
	writer.WriteDestFile ();
}

ALLEGRO_BITMAP* Terrain::generateHeightMap(void){
module::RidgedMulti mountainTerrain;
mountainTerrain.SetOctaveCount (1);

  module::Billow baseFlatTerrain;
  baseFlatTerrain.SetFrequency (2.0);

  module::ScaleBias flatTerrain;
  flatTerrain.SetSourceModule (0, baseFlatTerrain);
  flatTerrain.SetScale (0.125);
  flatTerrain.SetBias (-0.75);

  module::Perlin terrainType;
  terrainType.SetFrequency (0.5);
  terrainType.SetPersistence (0.25);
  terrainType.SetOctaveCount (1);

  module::Select terrainSelector;
  terrainSelector.SetSourceModule (0, flatTerrain);
  terrainSelector.SetSourceModule (1, mountainTerrain);
  terrainSelector.SetControlModule (terrainType);
  terrainSelector.SetBounds (0.0, 1000.0);
  terrainSelector.SetEdgeFalloff (0.1);

  module::Turbulence finalTerrain;
  finalTerrain.SetSourceModule (0, terrainSelector);
  finalTerrain.SetFrequency (4.0);
  finalTerrain.SetPower (0.125);

  utils::NoiseMap heightMap;
  utils::NoiseMapBuilderPlane heightMapBuilder;
  heightMapBuilder.SetSourceModule (finalTerrain);
  heightMapBuilder.SetDestNoiseMap (heightMap);
  heightMapBuilder.SetDestSize (xSize, ySize);
  heightMapBuilder.SetBounds (0.0, 4.0, 0.0, 4.0);
  heightMapBuilder.Build ();

  utils::RendererImage renderer;
  utils::Image image;
  renderer.SetSourceNoiseMap (heightMap);
  renderer.SetDestImage (image);
  /*renderer.ClearGradient ();
  renderer.AddGradientPoint (-1.00, utils::Color ( 32, 160,   0, 255)); // grass
  renderer.AddGradientPoint (-0.25, utils::Color (224, 224,   0, 255)); // dirt
  renderer.AddGradientPoint ( 0.25, utils::Color (128, 128, 128, 255)); // rock
  renderer.AddGradientPoint ( 1.00, utils::Color (255, 255, 255, 255)); // snow*/
  renderer.EnableLight ();
  renderer.SetLightContrast (3.0);
  renderer.SetLightBrightness (2.0);
  renderer.Render ();

  save(image, "save.bmp");

	return convertBMP(image);
}

ALLEGRO_BITMAP* Terrain::convertBMP(utils::Image image){
	ALLEGRO_BITMAP* bitmap;
	
	bitmap = al_create_bitmap(image.GetWidth(), image.GetHeight());
	al_lock_bitmap(bitmap, al_get_bitmap_format(bitmap), ALLEGRO_LOCK_WRITEONLY);
	al_set_target_bitmap(bitmap);

	int x,y;
	for (x=0; x<image.GetWidth(); x++){
		for (y=0; y<image.GetHeight(); y++){
			al_put_pixel(x, /*image.GetHeight()-*/y, convertColor(image.GetValue(x, y)));
		}
	}

	al_unlock_bitmap(bitmap);
	return bitmap;
}

ALLEGRO_COLOR Terrain::convertColor(utils::Color color){
	return al_map_rgba(float(color.red), float(color.green), float(color.blue), float(color.alpha));
}

void Terrain::camera_2D_setup(){
	/*glMatrixMode(GL_PROJECTION);
	glLoadIdentity();*/
	glOrtho(0, xSize, ySize, 0.0, 0.0, 1.0);
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
}

void Terrain::camera_3D_setup(){
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(35, (GLdouble)xSize / (GLdouble)ySize, 1.0, 2000.0);

	glRotatef(25.0f, 1.0f, 0.0f, 0.0f);
	glTranslatef(0.0f, -450.0f, -550.0f);
	glEnable(GL_DEPTH_TEST);
}

void Terrain::load_ht_map(ALLEGRO_BITMAP* heightMap, std::vector<GLfloat> &verts, std::vector<GLbyte> &colors, GLfloat land_scale, GLfloat height_scale){
    ALLEGRO_COLOR ht_pixel;
	GLfloat height_true = 0.0f;
    int bmp_x = 0;
    int bmp_z = 0;
    unsigned char r, g, b;
 
    // locking the bitmap makes the reading of it noticably faster...
    al_lock_bitmap(heightMap, ALLEGRO_PIXEL_FORMAT_ANY, ALLEGRO_LOCK_READONLY);
 
    // autocentering variables
    GLfloat cent_wd = GLfloat((xSize) / 2.0f) * land_scale;
    GLfloat cent_ht = GLfloat((ySize) / 2.0f) * land_scale;
 
    verts.reserve(ySize * xSize * 3);
	colors.reserve(ySize * xSize * 3);

	//for every x, z coord
	for(bmp_z = 0; bmp_z < ySize; bmp_z++){
		for(bmp_x = 0; bmp_x < xSize; bmp_x++){
			ht_pixel = al_get_pixel(heightMap, bmp_x, bmp_z); // get pixel color
			al_unmap_rgb(ht_pixel, &r, &g, &b);
			colors.push_back(r);
			colors.push_back(g);
			colors.push_back(b);

			//add x
			verts.push_back(GLfloat((bmp_x * land_scale) - cent_wd));

			//add y, color to greyscale conversion
			height_true = GLfloat(float(0.299*r + 0.587*g + 0.114*b) / 255.0f) * height_scale;
			verts.push_back(height_true);

			//add z
			verts.push_back(GLfloat((bmp_z * land_scale) - cent_ht));
		}
	}
    al_unlock_bitmap(heightMap);
}
 
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