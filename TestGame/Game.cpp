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
	manager = new ResourceManager();
	map = new Map();
	hud = new IngameHUD();
}

Game::~Game(void){
	map = NULL;
}

void Game::tick(){
	num++;
	map->draw();
}