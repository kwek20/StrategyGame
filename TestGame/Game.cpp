#include "Game.h"
#include "Status.h"

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
	camera = new Camera(map);
}

Game::~Game(void){
	delete map;
	delete hud;
	delete camera;
	delete manager;
}

void Game::tick(){
	num++;
	camera->camera_3D_setup(display);
	map->draw();
	camera->camera_2D_setup(display);
	hud->draw(this);
}

void Game::handleKeyboard(ALLEGRO_EVENT_TYPE type, ALLEGRO_KEYBOARD_STATE state){
	if (type == ALLEGRO_EVENT_KEY_DOWN){
		if (al_key_down(&state, ALLEGRO_KEY_W)){
			if (al_key_down(&state, ALLEGRO_KEY_S)) return;

			if (al_key_down(&state, ALLEGRO_KEY_D)){
				if (al_key_down(&state, ALLEGRO_KEY_A)) return;
				camera->move(UP_RIGHT);
			} else if (al_key_down(&state, ALLEGRO_KEY_A)){
				camera->move(LEFT_UP);
			} else {
				camera->move(UP);
			}
		} else if (al_key_down(&state, ALLEGRO_KEY_D)){
			if (al_key_down(&state, ALLEGRO_KEY_A)) return;

			if (al_key_down(&state, ALLEGRO_KEY_S)){
				camera->move(RIGHT_DOWN);
			} else {
				camera->move(RIGHT);
			}
		} else if (al_key_down(&state, ALLEGRO_KEY_S)){
			if (al_key_down(&state, ALLEGRO_KEY_A)){
				camera->move(DOWN_LEFT);
			} else {
				camera->move(DOWN);
			}
		} else if (al_key_down(&state, ALLEGRO_KEY_A)){
			camera->move(LEFT);
		}
	}
}

void Game::handleMouse(ALLEGRO_EVENT_TYPE type, ALLEGRO_MOUSE_STATE state){
	float width = al_get_display_width(display), height = al_get_display_height(display);
	if (type == ALLEGRO_EVENT_MOUSE_AXES){
		if (al_mouse_button_down(&state, 2)){
			if (mouseTempX < 0 || mouseTempY < 0){
				mouseTempX = state.x;
				mouseTempY = state.y;
			}

			camera->addPitch(float(state.y) - mouseTempY);
			camera->addYaw(float(state.x) - mouseTempX);
			al_set_mouse_xy(display, mouseTempX, mouseTempY);
		} else if (mouseTempX >= 0 || mouseTempY >= 0){
			//mouse released after dragging
			mouseTempX = -1;
			mouseTempY = -1;
		}
	}
}