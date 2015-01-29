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
	camera = new Camera(al_get_display_width(display), al_get_display_height(display));
}

Game::~Game(void){
	delete map;
	delete hud;
	delete camera;
	delete manager;
}

void Game::tick(double deltaTime){
	num++;
	camera->camera_3D_setup(display);
	map->draw();
	//camera->camera_2D_setup(display);
	camera->move(deltaTime);
	//hud->draw(this);
}

void Game::handleKeyboard(ALLEGRO_EVENT_TYPE type, int keycode){
	// If a key is pressed, toggle the relevant key-press flag
	if (type == ALLEGRO_EVENT_KEY_DOWN){
		switch (keycode) {
		case ALLEGRO_KEY_W:
			camera->holdingForward = true;
			break;
		case ALLEGRO_KEY_S:
			camera->holdingBackward = true;
			break;
		case ALLEGRO_KEY_A:
			camera->holdingLeftStrafe = true;
			break;
		case ALLEGRO_KEY_D:
			camera->holdingRightStrafe = true;
			break;
			/*case '[':
			fpsManager.setTargetFps(fpsManager.getTargetFps() - 10);
			break;
			case ']':
			fpsManager.setTargetFps(fpsManager.getTargetFps() + 10);
			break;*/
		default:
			// Do nothing...
			break;
		}
	} else if (type == ALLEGRO_EVENT_KEY_UP) { // If a key is released, toggle the relevant key-release flag 
		switch (keycode) {
		case ALLEGRO_KEY_W:
			camera->holdingForward = false;
			break;
		case ALLEGRO_KEY_S:
			camera->holdingBackward = false;
			break;
		case ALLEGRO_KEY_A:
			camera->holdingLeftStrafe = false;
			break;
		case ALLEGRO_KEY_D:
			camera->holdingRightStrafe = false;
			break;
		default:
			// Do nothing...
			break;
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

			camera->handleMouseMove(state.x - mouseTempX, state.y - mouseTempY);
			al_set_mouse_xy(display, mouseTempX, mouseTempY);
		} else if (mouseTempX >= 0 || mouseTempY >= 0){
			//mouse released after dragging
			mouseTempX = -1;
			mouseTempY = -1;
		}
	}
}