#include <stdio.h>

#include "main.h"
#include "Game.h"

#include <allegro5/allegro.h>
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

/*int main()
{
  al_init();

  auto dw = 800;
  auto dh = 600;
  
  al_set_new_display_option(ALLEGRO_DEPTH_SIZE, 16, ALLEGRO_SUGGEST);
  auto d = al_create_display(dw, dh);
  
  if(!d)
  {
    printf("Failed to create display\n");
    return 1;
  }
  
  al_install_keyboard();
  
  auto queue = al_create_event_queue();
  auto timer = al_create_timer(1.0 / 60.0);
  
  al_register_event_source(queue, al_get_timer_event_source(timer));
  al_register_event_source(queue, al_get_keyboard_event_source());
  al_start_timer(timer);
  
  al_init_primitives_addon();
  
  int ind[] = 
  {
    0, 1, 2,
    0, 1, 3,
    0, 2, 3,
    1, 2, 3
  };
  
  ALLEGRO_VERTEX vtx[4];
  vtx[0].x = -200;
  vtx[0].y = -200;
  vtx[0].z = -200;
  vtx[0].color = al_map_rgb_f(0, 0, 1);

  vtx[1].x = 0;
  vtx[1].y = -200;
  vtx[1].z = 200;
  vtx[1].color = al_map_rgb_f(0, 1, 0);

  vtx[2].x = 200;
  vtx[2].y = -200;
  vtx[2].z = -200;
  vtx[2].color = al_map_rgb_f(1, 0, 1);

  vtx[3].x = 0;
  vtx[3].y = 200;
  vtx[3].z = 0;
  vtx[3].color = al_map_rgb_f(1, 1, 1);

  float theta = 0;
  ALLEGRO_TRANSFORM transform;

  int mode = 0;
  bool done = false;
  bool redraw = true;
  while(!done)
  {
    ALLEGRO_EVENT ev;
    al_wait_for_event(queue, &ev);
    switch(ev.type)
    {
      case ALLEGRO_EVENT_KEY_DOWN:
        switch(ev.keyboard.keycode)
        {
          case ALLEGRO_KEY_ESCAPE:
            done = true;
            break;
        }
        break;
      case ALLEGRO_EVENT_TIMER:
        theta += 0.01;
        redraw = true;
        break;
    }

    if(al_is_event_queue_empty(queue) && redraw)
	{
	  al_set_target_bitmap(al_get_backbuffer(d));
  
	  // Set the projection transform back
	  al_identity_transform(&transform);
	  al_perspective_transform(&transform, -dw / 2, -dh / 2, dw / 2, dw / 2, dh / 2, 10000);
	  al_set_projection_transform(d, &transform);

	  al_clear_to_color(al_map_rgb_f(0, 0, 0));
	  al_clear_depth_buffer(1000);
	  al_set_render_state(ALLEGRO_DEPTH_TEST, 1);
  
	  al_identity_transform(&transform);
	  al_rotate_transform_3d(&transform, 0, 1, 0, theta);
	  al_translate_transform_3d(&transform, 100, 0, -800);
	  al_use_transform(&transform);
  
	  al_draw_indexed_prim(vtx, NULL, NULL, ind, 12, ALLEGRO_PRIM_TRIANGLE_LIST);
  
	  al_identity_transform(&transform);
	  al_rotate_transform_3d(&transform, 0, 1, 0, theta / 2);
	  al_translate_transform_3d(&transform, -100, 0, -800);
	  al_use_transform(&transform);
  
	  al_draw_indexed_prim(vtx, NULL, NULL, ind, 12, ALLEGRO_PRIM_TRIANGLE_LIST);
  
	  al_flip_display();
  
	  redraw = false;
	}
  }
}*/

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
	bool redraw = true;
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
			}
			break;
		  case ALLEGRO_EVENT_TIMER:
			theta += 0.01;
			redraw = true;
			break;
		}

		if(al_is_event_queue_empty(event_queue) && redraw){
			al_set_target_backbuffer(display);
			al_clear_to_color(al_map_rgba(0,0,0,0));
			game->tick();
			al_flip_display();
			redraw = false;
		}
	}

	i = shutdown(game->getShutDownReason());
	log("Thank you for playing!\n");

	for (int i=0; i<1000; i++){}

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

void log(std::string message){
	fprintf(stdout, message.c_str());
}

void error(string message){
	fprintf(stderr, message.c_str());
}