#ifndef __MAIN_H_INCLUDED__
#define __MAIN_H_INCLUDED__

#define _CRTDBG_MAP_ALLOC_NEW
#include <stdlib.h>
#include <crtdbg.h>

#pragma once

#include <allegro5\allegro.h>
#include <allegro5\allegro_opengl.h>

#include <allegro5\allegro_primitives.h>
#include <allegro5\allegro_font.h>
#include <allegro5\allegro_ttf.h>
#include <allegro5\allegro_image.h>
#include <allegro5\mouse.h>

#include <string>
#include "ResourceManager.h"
#include "FpsManager.hpp"

#define FPS 60
#define NAME "Strategy Game"

using namespace std;

const static struct {
	bool (*func)();
	const string name;
} load_functions[] = {
	{al_init_primitives_addon, "primitives"},
	{al_install_keyboard, "keyboard"},
	{al_install_mouse, "mouse"},
	{al_init_font_addon, "font"},
	{al_init_ttf_addon, "ttf fonts"},
	{al_init_image_addon, "images"}
};

ALLEGRO_CONFIG *cfg;
ALLEGRO_DISPLAY *display = NULL;
ALLEGRO_EVENT_QUEUE *event_queue = NULL;
ALLEGRO_TIMEOUT timeout;

// Create a FPS manager that locks to 60fps and updates the window title with stats every 3 seconds
FpsManager *fpsManager;

void log(string message);
void error(string message);

void setGlFlags();

int shutdown(string reason);

inline int ale_screenshot(const char *destination_path, const char *folder, const char *gamename);

#endif