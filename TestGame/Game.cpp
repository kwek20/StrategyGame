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
	camera = new Camera();
}

Game::~Game(void){
	map = NULL;
}

void Game::tick(){
	num++;
	camera->camera_3D_setup(display);
	map->draw();
	camera->camera_2D_setup(display);
	hud->draw();
}

void Game::handleKeyboard(ALLEGRO_EVENT_TYPE type, ALLEGRO_KEYBOARD_STATE state){

}

void Game::handleMouse(ALLEGRO_EVENT_TYPE type, ALLEGRO_MOUSE_STATE state){
	if (type == ALLEGRO_EVENT_MOUSE_AXES){
		std::cout << "Mouse event! " << state.x << " " << state.y << "\n";
		std::cout << "new rotatin: " << (float(state.y) / float(al_get_display_height(display)) * 90) << "\n";
		camera->setAngle(float(state.y) / float(al_get_display_height(display)) * 90);
		camera->setRotation(float(state.x) / float(al_get_display_width(display)) * 360);
	}
}