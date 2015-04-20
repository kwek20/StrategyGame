#ifndef __RESOURCELOADER_H_INCLUDED__
#define __RESOURCELOADER_H_INCLUDED__

#pragma once

#include <iostream>

#include <allegro5\file.h>
#include <allegro5\bitmap_io.h>
#include <allegro5\allegro_audio.h>
#include <allegro5\allegro_ttf.h>
#include <allegro5\bitmap.h>

#include <vector>

template <class type>
struct Data {
	std::string path;
	type *data;
};

class IResourceLoader {
public:
	IResourceLoader(std::string p){_name = p;}

	virtual void load() = 0;
	std::string getName(){ return _name;}

private:
	std::string _name;
};

template <class type>
class ResourceLoader : public IResourceLoader
{
public:
	ResourceLoader<type>(std::string p) : IResourceLoader(p) {
		//append name to resource path
		al_append_path_component(path = al_get_standard_path(ALLEGRO_RESOURCES_PATH), p.c_str());
	}

	~ResourceLoader<type>(void){
		al_destroy_path(path);

		for (const Data<type> var : data){
			//delete var.data;
		}
	}

	ALLEGRO_PATH *getPath(){return path;}
	void addData(Data<type> d){data.push_back(d); std::cout << "Loaded \"" << d.path.c_str() << "\"\n";}

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
			if (stricmp(name.c_str(), tempData.path.c_str()) == 0) return tempData.data;
		}
		return NULL;
	}

	virtual void loadDataFile(int delSize, std::string fileName){
		int end = fileName.find_last_of('.');
		Data<type> d;
		//strip resource path and file extension
		d.path = fileName.substr(delSize, end-delSize);
		d.data = loadFile(fileName.c_str());
		if (!d.data){ std::cout << "Cant load file " << d.path.c_str() << "\n"; } else {
			addData(d);
		}
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
					loadDataFile(size, al_get_fs_entry_name(next));
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

class FontLoader : public ResourceLoader<ALLEGRO_FONT>
{
public:
	FontLoader(std::string p) : ResourceLoader(p){};
	ALLEGRO_FONT *loadFile(const char *fileName);
};

class ImageLoader : public ResourceLoader<ALLEGRO_BITMAP>
{
public:
	ImageLoader(std::string p) : ResourceLoader(p){};
	ALLEGRO_BITMAP *loadFile(const char *fileName);
};

class SoundLoader : public ResourceLoader<ALLEGRO_SAMPLE>
{
public:
	SoundLoader(std::string p) : ResourceLoader(p){};
	ALLEGRO_SAMPLE *loadFile(const char *fileName);
};

#endif