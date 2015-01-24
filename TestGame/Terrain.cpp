#include "Terrain.h"
#include "noiseutils.h"
#include <gl\glu.h>
#include <vector>
#include <iostream>

Terrain::Terrain(int x, int y, int angle){
	xSize = x < 1 ? 1 : x;
	ySize = y < 1 ? 1 : y;
	this->angle = angle < -360 ? 360 : (angle > 360 ? 360 : angle);

	std::cout << "Generating heightmap.\n";
	ALLEGRO_BITMAP* heightMap = generateHeightMap();

	std::cout << "Loading verticles..\n";
	load_ht_map(heightMap, land_verticles);

	std::cout << "Making connection points...\n";
	make_point_connections(connect_points);

	al_destroy_bitmap(heightMap);
}

Terrain::~Terrain(void){

}

void Terrain::draw(void){
	camera_3D_setup();

	//select model stack
	glMatrixMode(GL_MODELVIEW);

	//reset  matrix
	glLoadIdentity();
	
	//rotate to angle
	glRotatef(angle, 0.0f, 1.0f, 0.0f);

	//enable array for use during rendering
	glEnableClientState(GL_VERTEX_ARRAY);

	//give vertex info, 3 points(triangle), using floats, array position
	glVertexPointer(3, GL_FLOAT, 0, &land_verticles.at(0));

	//draw elements, type triangle, array size, object type, array position,
	glDrawElements(GL_TRIANGLES, connect_points.size(), GL_UNSIGNED_INT, &connect_points.at(0));

	angle += 1;
	if (angle >= 360) angle = -360;
}

/*
void Terrain::save(std::string name){
	utils::WriterBMP writer;
	writer.SetSourceImage (image);
	writer.SetDestFilename (name);
	writer.WriteDestFile ();
}
*/

ALLEGRO_BITMAP* Terrain::generateHeightMap(void){
	module::RidgedMulti mountainTerrain;
	mountainTerrain.SetFrequency(0.5);

	 module::Billow baseFlatTerrain;
	 baseFlatTerrain.SetFrequency (6.0);

	 module::ScaleBias flatTerrain;
	 flatTerrain.SetSourceModule (0, baseFlatTerrain);
	 flatTerrain.SetScale (0.05);
	 flatTerrain.SetBias (0.25);

	 module::Perlin terrainType;
	 terrainType.SetFrequency (0.5);
	 terrainType.SetPersistence (0.25);

	 module::Select finalTerrain;
	 finalTerrain.SetSourceModule (0, flatTerrain);
	 finalTerrain.SetSourceModule (0.5, mountainTerrain);
	 finalTerrain.SetSourceModule (1, mountainTerrain);
	 finalTerrain.SetControlModule (terrainType);
	 finalTerrain.SetBounds (0.0, 1000);
	 finalTerrain.SetEdgeFalloff (2);

	utils::NoiseMap heightMap;
	  utils::NoiseMapBuilderPlane heightMapBuilder;
	  heightMapBuilder.SetSourceModule (finalTerrain);
	  heightMapBuilder.SetDestNoiseMap (heightMap);
	  heightMapBuilder.SetDestSize (xSize, ySize);
	  heightMapBuilder.SetBounds (6.0, 10.0, 1.0, 5.0);
	  heightMapBuilder.Build ();

	  utils::RendererImage renderer;
	  utils::Image image;
	  renderer.SetSourceNoiseMap (heightMap);
	  renderer.SetDestImage (image);
	  
	  renderer.EnableLight ();
	  renderer.SetLightContrast (3.0);
	  renderer.SetLightBrightness (2.0);
	  renderer.Render ();

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
			al_put_pixel(x, image.GetHeight()-y, convertColor(image.GetValue(x, y)));
		}
	}

	al_unlock_bitmap(bitmap);
	return bitmap;
}

ALLEGRO_COLOR Terrain::convertColor(utils::Color color){
	return al_map_rgba(float(color.red), float(color.green), float(color.blue), float(color.alpha));
}

void Terrain::camera_2D_setup(){
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0,0, (GLdouble)xSize / (GLdouble)ySize, 0.0, -1.0, 1.0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void Terrain::camera_3D_setup(){
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(90.0, (GLdouble)xSize / (GLdouble)ySize, 1.0, 2000.0);

	glRotatef(25.0f, 1.0f, 0.0f, 0.0f);
	glTranslatef(0.0f, -250.0f, -550.0f);
}

void Terrain::load_ht_map(ALLEGRO_BITMAP* heightMap, std::vector<GLfloat> &verts, GLfloat land_scale, GLfloat height_scale){
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
 
	//for every x, z coord
    for(bmp_z = 0; bmp_z < ySize; bmp_z++){
        for(bmp_x = 0; bmp_x < xSize; bmp_x++){
			//add x
            verts.push_back(GLfloat((bmp_x * land_scale) - cent_wd));
 
            ht_pixel = al_get_pixel(heightMap, bmp_x, bmp_z); // get pixel color
            al_unmap_rgb(ht_pixel, &r, &g, &b);
            height_true = GLfloat(float(r) / 255.0f) * height_scale;
 
            verts.push_back(height_true);
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