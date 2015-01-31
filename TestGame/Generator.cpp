#include "Generator.h"


Generator::Generator(void){}


Generator::~Generator(void){}

ALLEGRO_BITMAP* FlagGenerator::generate(int xSize, int ySize){
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

	renderer.EnableLight ();
	renderer.SetLightContrast (3.0);
	renderer.SetLightBrightness (2.0);
	renderer.Render ();

	return convertBMP(image);
}

ALLEGRO_BITMAP* Generator::convertBMP(utils::Image image){
	ALLEGRO_BITMAP* bitmap;
	
	bitmap = al_create_bitmap(image.GetWidth(), image.GetHeight());
	al_lock_bitmap(bitmap, al_get_bitmap_format(bitmap), ALLEGRO_LOCK_WRITEONLY);
	al_set_target_bitmap(bitmap);

	int x,y;
	for (x=0; x<image.GetWidth(); x++){
		for (y=0; y<image.GetHeight(); y++){
			al_put_pixel(x, y, convertColor(image.GetValue(x, y)));
		}
	}

	al_unlock_bitmap(bitmap);
	return bitmap;
}

ALLEGRO_COLOR Generator::convertColor(utils::Color color){
	return al_map_rgba(color.red, color.green, color.blue, color.alpha);
}

