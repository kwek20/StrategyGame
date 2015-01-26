#ifndef __RESOURCES_H_INCLUDED__
#define __RESOURCES_H_INCLUDED__

#pragma once

#include <iostream>

#include <allegro5\file.h>
#include <allegro5\bitmap_io.h>
#include <allegro5\allegro_audio.h>
#include <allegro5\allegro_font.h>
#include <allegro5\bitmap.h>

#include <vector>

struct IResource {
	virtual void load() = 0;
};

template <class type>
class Resource : virtual public IResource
{
public:
	Resource(std::string p) {
		al_append_path_component(path = al_get_standard_path(ALLEGRO_RESOURCES_PATH), p.c_str());
		std::cout << "Loading " << p.c_str() << "\n";
		//load();
	}

	~Resource(void){al_destroy_path(path);}
	virtual void load() = 0;
private:
	std::vector<type*> data;
private:
	ALLEGRO_PATH *path;
};

class Font : virtual public Resource<ALLEGRO_FONT>
{
public:
	Font(std::string p) : Resource(p){};
	void load();
};

class Image : virtual public Resource<ALLEGRO_BITMAP>
{
public:
	Image(std::string p) : Resource(p){};
	void load();
};

class Sound : virtual public Resource<ALLEGRO_SAMPLE>
{
public:
	Sound(std::string p) : Resource(p){};
	void load();
};

#endif