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

template <class type>
struct Data {
	std::string path;
	type *data;
};

class IResource {
public:
	IResource(std::string p){_name = p;}

	virtual void load() = 0;
	std::string getName(){ return _name;}

private:
	std::string _name;
};

template <class type>
class Resource : public IResource
{
public:
	Resource<type>(std::string p) : IResource(p) {
		//append name to resource path
		al_append_path_component(path = al_get_standard_path(ALLEGRO_RESOURCES_PATH), p.c_str());
	}

	~Resource<type>(void){al_destroy_path(path);}
	ALLEGRO_PATH *getPath(){return path;}
	void addData(Data<type> d){data.push_back(d);}

	void load(){
		std::cout << "Loading resource " << getName().c_str() << "\n";
		ALLEGRO_PATH *path = getPath();
		ALLEGRO_FS_ENTRY *entry = al_create_fs_entry(al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP));

		if (!(al_get_fs_entry_mode(entry) & ALLEGRO_FILEMODE_ISDIR)) return;
		//load folder, it exists
		loadFolder(entry);
		//cleanup
		al_destroy_fs_entry(entry);
	}

	type *getData(std::string name){
		Data<type> tempData;

		//loop over data
		for (unsigned int i=0; i<data.size(); i++){
			tempData = data.at(i);
			//if name equals data name
			if (strcmp(name.c_str(), tempData.path.c_str()) == 0) return tempData.data;
		}
		return NULL;
	}

	void loadFolder(ALLEGRO_FS_ENTRY *folder){
		//can we open it? (permissions related)
		if (!al_open_directory(folder)) {
			return;
		}

		ALLEGRO_FS_ENTRY *next;
		std::string pathName = al_get_fs_entry_name(folder);
		int size = strlen(pathName.c_str()) + 1;

		//loop untill we dont have files
		while (true){
			//read next file in directory
			next = al_read_directory(folder);

			//we have a winner
			if (next) {
				//is it a file or a directory?
				if (al_get_fs_entry_mode(next) & ALLEGRO_FILEMODE_ISFILE) {
					//load this file
					std::string fileName = al_get_fs_entry_name(next);
					int end = fileName.find_last_of('.');

					Data<type> d;
					//strip resource path and file extension
					d.path = fileName.substr(size, end-size);
					d.data = loadFile(fileName.c_str());
					addData(d);
				} else if (al_get_fs_entry_mode(next) & ALLEGRO_FILEMODE_ISDIR){
					//load this folder too!
					loadFolder(next);
				}
				//clean up the mess
				al_destroy_fs_entry(next);
			} else {
				break;
			}
		}
	}

	virtual type* loadFile(const char *fileName) = 0;
private:
	std::vector< Data<type> > data;
	ALLEGRO_PATH *path;
};

class Font : public Resource<ALLEGRO_FONT>
{
public:
	Font(std::string p) : Resource(p){};
	ALLEGRO_FONT *Font::loadFile(const char *fileName);
};

class Image : public Resource<ALLEGRO_BITMAP>
{
public:
	Image(std::string p) : Resource(p){};
	ALLEGRO_BITMAP *Image::loadFile(const char *fileName);
};

class Sound : public Resource<ALLEGRO_SAMPLE>
{
public:
	Sound(std::string p) : Resource(p){};
	ALLEGRO_SAMPLE *Sound::loadFile(const char *fileName);
};

#endif