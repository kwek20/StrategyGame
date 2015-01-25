#include <stdio.h>

#include "main.h"
#include "Game.h"

#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>

#include <noise/noise.h>

using namespace std;

const static struct {
	bool (*func)();
	const string name;
} load_functions[] = {
	{al_init_primitives_addon, "primitives"},
	{al_install_keyboard, "keyboard"},
	{al_install_mouse, "mouse"},
	{al_init_font_addon, "font"}
};

ALLEGRO_CONFIG *cfg;
ALLEGRO_DISPLAY *display = NULL;
ALLEGRO_EVENT_QUEUE *event_queue = NULL;
ALLEGRO_TIMEOUT timeout;

int main(int argc, char **argv){
	Game *game;
	int i;

	log("Initializing...\n");
	if(!al_init()) {
		error("failed to initialize allegro!\n");
		return -1;
	}

	//load all libraries we need
	for (i=0; i < (sizeof(load_functions) / sizeof(load_functions[0])); i++){
		if (!load_functions[i].func()){
			error("failed to load " + load_functions[i].name + "!\n");
			return -1;
		} else {log("Loaded "+load_functions[i].name + "\n");}
	}
	log("Loaded libraries\n");

	al_set_new_display_option(ALLEGRO_VSYNC, 1, ALLEGRO_SUGGEST);
	al_set_new_display_flags(ALLEGRO_OPENGL);
	display = al_create_display(800, 600);
	if(!display) {
		error("failed to create display!\n");
		return -1;
	}

	event_queue = al_create_event_queue();
	if(!event_queue) {
		error("failed to create event queue!\n");
		return -1;
	}

	auto timer = al_create_timer(1.0 / 60.0);

	al_register_event_source(event_queue, al_get_timer_event_source(timer));
	al_register_event_source(event_queue, al_get_keyboard_event_source());
	al_register_event_source(event_queue, al_get_mouse_event_source());
	al_register_event_source(event_queue, al_get_display_event_source(display));

	al_start_timer(timer);
	
	game = new Game(display);
	bool redraw = true, pause = false;
	float theta = 0;

	log("Initializing done!\n");

	while (!game->shouldShutDown()){
		ALLEGRO_EVENT ev;
		al_wait_for_event(event_queue, &ev);
		switch(ev.type){
		  case ALLEGRO_EVENT_KEY_DOWN:
			switch(ev.keyboard.keycode){
			  case ALLEGRO_KEY_ESCAPE:
				  game->shutDown();
				  break;
			  case ALLEGRO_KEY_SPACE:
				  pause = !pause;
			}
			break;
		  case ALLEGRO_EVENT_TIMER:
			theta += 0.01;
			redraw = true;
			break;
		}

		if(al_is_event_queue_empty(event_queue) && redraw && !pause){
			al_set_target_backbuffer(display);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			al_clear_to_color(al_map_rgba(0,0,0,0));
			game->tick();
			glFlush();
			al_flip_display();
			redraw = false;
		}
	}

	i = shutdown(game->getShutDownReason());
	log("Thank you for playing!\n");

	return i;
}

int shutdown(string reason = ""){
	log("Shutting down the game");
	if (strcmp(reason.c_str(), "") != 0) log(" because of " + reason);
	log("\n");

	/* SLOW SHUTDOWN, annoying for testing
	al_destroy_display(display);

	al_flush_event_queue(event_queue);
	al_destroy_event_queue(event_queue);

	al_shutdown_font_addon();
	al_shutdown_primitives_addon();*/
	return 0;
}

void log(std::string message){
	fprintf(stdout, message.c_str());
}

void error(string message){
	fprintf(stderr, message.c_str());
}