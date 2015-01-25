#ifndef __MAIN_H_INCLUDED__
#define __MAIN_H_INCLUDED__

#pragma once

#include <allegro5/allegro.h>
#include <allegro5/allegro_opengl.h>

#include <string>

#define FPS 60

using namespace std;

void log(string message);
void error(string message);

int shutdown(string reason);
void save_screenshot(std::string name="screenshot");

#endif