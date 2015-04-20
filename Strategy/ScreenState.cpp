#include "ScreenState.h"

void ScreenState::start(Game *game, ScreenState *oldState){

}

void ScreenState::end(Game *game){

}

ALLEGRO_DISPLAY *ScreenState::getDisplay(){
	return display;
}

int ScreenState::getDisplayWidth(){
	return al_get_display_width(display);
}

int ScreenState::getDisplayHeight(){
	return al_get_display_height(display);
}

bool ScreenState::shouldShutDown(){ 
	return shutdown; 
}

void ScreenState::shutDown(){ 
	shutdown = true; 
}

std::string ScreenState::getShutDownReason(void){ 
	return shutdownReason; 
}

void ScreenState::setShutDownReason(std::string reason){ 
	shutdownReason = reason; 
}

void ScreenState::log(std::string message){ 
	fprintf(stdout, message.c_str()); 
}

void ScreenState::error(std::string message){ 
	fprintf(stderr, message.c_str()); 
}