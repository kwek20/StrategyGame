#include "Game.h"

#include <time.h>
#include <iostream>

#include <allegro5\allegro_font.h>

using namespace std;

Game::Game(ALLEGRO_DISPLAY* display){
	log("Loading game\n");
	shutdown = false;
	num = 0;

	this->display = display;
	map = new Map();
}

Game::~Game(void){

}

void Game::tick(){
	num++;
	map->draw();
	al_draw_text(al_create_builtin_font(), al_map_rgb(255,0,0), 10, 10, 0, ("tick: " + to_string(num)).c_str());
}