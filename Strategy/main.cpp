#include "main.h"

#include "Game.h"
#include "LoadState.h"

#include <time.h>
#include <memory>

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
	al_set_new_display_option(ALLEGRO_SAMPLE_BUFFERS, 1, ALLEGRO_SUGGEST);
	al_set_new_display_option(ALLEGRO_SAMPLES, 3, ALLEGRO_SUGGEST);

	al_set_new_display_flags(ALLEGRO_OPENGL);
	setGlFlags();


	display = al_create_display(800, 600);
	if(!display) {
		error("failed to create display!\n");
		return -1;
	}

	al_set_window_title(display, std::string(NAME).append(" | ").append(to_string(FPS) + " FPS").c_str());
	event_queue = al_create_event_queue();
	if(!event_queue) {
		error("failed to create event queue!\n");
		return -1;
	}

	al_register_event_source(event_queue, al_get_keyboard_event_source());
	al_register_event_source(event_queue, al_get_mouse_event_source());
	al_register_event_source(event_queue, al_get_display_event_source(display));
	
	fpsManager = new FpsManager(FPS, 1.0, std::string(NAME), display);
	game = new Game();
	game->setScreenState(new LoadState(display));

	bool pause = false;
	double deltaTime = 0.50;

	al_grab_mouse(display);
	//ALLEGRO_BITMAP *mouseCursor = game->getManager()->getImage("empty");
	//al_set_mouse_cursor(display, al_create_mouse_cursor(mouseCursor, 20, 20));
	//al_show_mouse_cursor(display);

	log("Initializing done!\n");

	while (!game->shouldShutDown()){
		ALLEGRO_EVENT ev;
		ALLEGRO_TIMEOUT timeout;

		//redraws even with no input
		al_init_timeout(&timeout, 1/FPS); // we dont need 1/FPS, this is less intensive

		bool get_event = al_wait_for_event_until(event_queue, &ev, &timeout);
		if (get_event){
			if (!pause) game->handleEvent(ev);

			switch(ev.type){
			case ALLEGRO_EVENT_KEY_UP:
				switch(ev.keyboard.keycode){
				case ALLEGRO_KEY_ESCAPE:
					game->shutDown();
					break;
				case ALLEGRO_KEY_SPACE:
					pause = !pause;
					//game->togglePause();
					break;
				case ALLEGRO_KEY_F12:
					ale_screenshot(NULL, "screenshots", NULL);
					break;
				}
			}
		}

		if(al_is_event_queue_empty(event_queue) && !pause){
			al_set_target_backbuffer(display);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			al_clear_to_color(al_map_rgb_f(0,0,0));
			game->tick(deltaTime);
			deltaTime = fpsManager->enforceFPS();
			glFlush();
			al_flip_display();
		}
	}

	al_ungrab_mouse();
	i = shutdown(game->getShutDownReason());
	log("Thank you for playing!\n");
	return i;
}

int shutdown(string reason = ""){
	log("Shutting down the game");
	if (strcmp(reason.c_str(), "") != 0) log(" because of " + reason);
	log("\n");

	al_destroy_display(display);

	al_flush_event_queue(event_queue);
	al_destroy_event_queue(event_queue);

	al_shutdown_font_addon();
	al_shutdown_primitives_addon();
	return 0;
}


// Save a copy of the current target_bitmap (usually what's on the screen).
// The screenshot is placed in `destination_path`.
// The filename will follow the format "`gamename`-YYYYMMDD[a-z].png"
// Where [a-z] starts at 'a' and increases (towards 'z') to prevent duplicates
// on the same day.
// This filename format allows for easy time-based sorting in a file manager,
// even if the "Modified Date" or other file information is lost.
//
// Arguments:
// `destination_path` - Where to put the screenshot. If NULL, uses
//      ALLEGRO_USER_DOCUMENTS_PATH.
//
// `folderName` - What folder to put the screenshot. If NULL, uses
//      the parent folder
//
// `gamename` - The name of your game (only use path-valid characters).
//      If NULL, uses al_get_app_name().
//
//
// Returns:
// 0 on success, anything else on failure.
inline int ale_screenshot(const char *destination_path, const char* folderName, const char *gamename){
	ALLEGRO_PATH *path;
	char *filename, *filename_wp;
	struct tm *tmp = (tm*)malloc(sizeof(tm)+1);
	time_t t;
	unsigned int i;
	const char *path_cstr;
 
	if(destination_path == NULL || !destination_path)
		path = al_get_standard_path(ALLEGRO_USER_DOCUMENTS_PATH);
	else
		path = al_create_path_for_directory(destination_path);
 
	if(!path)
		return -1;
 
	if(!gamename) {
		if( !(gamename = al_get_app_name()) ) {
			al_destroy_path(path);
			return -2;
		}
	}
 
	t = time(0);
	tmp = localtime(&t);
	if(!tmp) {
		al_destroy_path(path);
		return -3;
	}
 
	if (folderName != NULL && folderName)al_append_path_component(path, folderName);
	al_make_directory(al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP));

	// Length of gamename + length of "-YYYYMMDD" + length of maximum [a-z] + NULL terminator
	if ( !(filename_wp = filename = (char *)malloc(strlen(gamename) + 9 + 2 + 1)) ) {
		al_destroy_path(path);
		return -4;
	}
 
	strcpy(filename, gamename);
	// Skip to the '.' in the filename, or the end.
	for(; *filename_wp != '.' && *filename_wp != 0; ++filename_wp);
 
	*filename_wp++ = '-';
	if(strftime(filename_wp, 9, "%Y%m%d", tmp) != 8) {
		free(filename);
		al_destroy_path(path);
		return -5;
	}
	filename_wp += 8;
 
	for(i = 0; i < 26*26; ++i) {
		if(i > 25) {
			filename_wp[0] = (i / 26) + 'a';
			filename_wp[1] = (i % 26) + 'a';
			filename_wp[2] = 0;
		}
		else {
			filename_wp[0] = (i % 26) + 'a';
			filename_wp[1] = 0;
		}
 
		al_set_path_filename(path, filename);
		al_set_path_extension(path, ".png");
		path_cstr = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);

		if (al_filename_exists(path_cstr))
			continue;
		
		log(std::string("Saved screenshot at ") + path_cstr + "\n");
		al_save_bitmap(path_cstr, al_get_target_bitmap());
		free(filename);
		al_destroy_path(path);
		return 0;
	}
 
	free(filename);
	al_destroy_path(path);
 
	return -6;
}

void setGlFlags(){
	glEnable(GL_BLEND); //alpha for glColor4f()
}

void log(std::string message){
	fprintf(stdout, message.c_str());
}

void error(string message){
	fprintf(stderr, message.c_str());
}