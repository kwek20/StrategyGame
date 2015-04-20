#include "Game.h"

Game::Game(){
	
}

ResourceManager * Game::getManager(){
	return manager;
}

void Game::setManager(ResourceManager *manager){
	this->manager = manager;
}

void Game::shutDown(){
	if (state) state->shutDown();
}

bool Game::shouldShutDown(){
	if (state) return state->shouldShutDown();
	return false;
}

std::string Game::getShutDownReason(){
	if (state) return state->getShutDownReason();
	return "No state!";
}

void Game::setScreenState(ScreenState *state){
	if (!state) return;

	if (this->state) state->end(this);
	state->start(this, this->state);
	this->state = state;
}

void Game::tick(float deltatime){
	if (state) state->tick(deltatime);
}

void Game::handleEvent(ALLEGRO_EVENT ev){
	switch (ev.type) {
		case ALLEGRO_EVENT_KEY_DOWN:
		case ALLEGRO_EVENT_KEY_UP: {
			ALLEGRO_KEYBOARD_STATE state;
			al_get_keyboard_state(&state);
			this->state->handleKeyboard(ev.type, ev.keyboard.keycode);
			break;
		}
		case ALLEGRO_EVENT_MOUSE_AXES:
		case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
		case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
		case ALLEGRO_EVENT_MOUSE_ENTER_DISPLAY:
		case ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY: {
			ALLEGRO_MOUSE_STATE state;
			al_get_mouse_state(&state);
			this->state->handleMouse(ev.type, state);
			break;				 
		}
	}
}