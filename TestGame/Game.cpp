#include "Game.h"
#include "Status.h"
#include "Player.h"

#include <vector>
#include <time.h>
#include <iostream>

#include <allegro5\allegro_font.h>

Game::Game(ALLEGRO_DISPLAY* display){
	log("Loading game\n");
	shutdown = false;

	this->display = display;
	manager = new ResourceManager();
	map = new Map();
	hud = new IngameHUD();

	controller = new PlayerController(map->getEntitiesByClass<Player>().at(0));
	camera = new Camera(al_get_display_width(display), al_get_display_height(display), *controller);
}

Game::~Game(void){
	delete map;
	delete hud;
	delete camera;
	delete manager;
}

void Game::tick(double deltaTime){
	controller->move(deltaTime);

	camera->camera_3D_setup(display);
	map->draw();

	camera->camera_2D_setup(display);
	hud->draw(this);
}

void Game::handleKeyboard(ALLEGRO_EVENT_TYPE type, int keycode){
	std::cout << "keyboard event " << keycode << "\n";
	// If a key is pressed, toggle the relevant key-press flag
	if (type == ALLEGRO_EVENT_KEY_DOWN){
		switch (keycode) {
		case ALLEGRO_KEY_W:
			controller->holdingForward = true;
			break;
		case ALLEGRO_KEY_S:
			controller->holdingBackward = true;
			break;
		case ALLEGRO_KEY_A:
			controller->holdingLeftStrafe = true;
			break;
		case ALLEGRO_KEY_D:
			controller->holdingRightStrafe = true;
			break;
		default:
			// Do nothing...
			break;
		}
	} else if (type == ALLEGRO_EVENT_KEY_UP) { // If a key is released, toggle the relevant key-release flag 
		switch (keycode) {
		case ALLEGRO_KEY_W:
			controller->holdingForward = false;
			break;
		case ALLEGRO_KEY_S:
			controller->holdingBackward = false;
			break;
		case ALLEGRO_KEY_A:
			controller->holdingLeftStrafe = false;
			break;
		case ALLEGRO_KEY_D:
			controller->holdingRightStrafe = false;
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

			controller->handleMouseMove(state.x - mouseTempX, state.y - mouseTempY);
			al_set_mouse_xy(display, mouseTempX, mouseTempY);
		} else if (mouseTempX >= 0 || mouseTempY >= 0){
			//mouse released after dragging
			mouseTempX = -1;
			mouseTempY = -1;
		}
	}
}