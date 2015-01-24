#include "Terrain.h"
#include "noiseutils.h"

#include <iostream>

Terrain::Terrain(int x, int y){
	xSize = x < 1 ? 1 : x;
	ySize = y < 1 ? 1 : y;

	std::cout << "Generating heightmap...\n";
	generateHeightMap();
}

Terrain::~Terrain(void){

}

void Terrain::draw(void){
	al_draw_bitmap(heightMap, 0, 0, 0);
}

/*
void Terrain::save(std::string name){
	utils::WriterBMP writer;
	writer.SetSourceImage (image);
	writer.SetDestFilename (name);
	writer.WriteDestFile ();
}
*/

void Terrain::generateHeightMap(void){
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

	  this->heightMap = convertBMP(image);
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