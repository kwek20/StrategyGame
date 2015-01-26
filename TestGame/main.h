#ifndef __MAIN_H_INCLUDED__
#define __MAIN_H_INCLUDED__

#pragma once

#include <allegro5/allegro.h>
#include <allegro5/allegro_opengl.h>

#include <string>

#define FPS 60
#define NAME "StrategyGame"

using namespace std;

void log(string message);
void error(string message);

int shutdown(string reason);
inline int ale_screenshot(const char *destination_path, const char *folder, const char *gamename);

#endif