#include "HUD.h"
#include "Game.h"

HUD::HUD(void)
{
}


HUD::~HUD(void)
{
}


void IngameHUD::draw(Game *game){
	std::string s = "Position: ";
	s.append(std::to_string(game->getController()->getTarget()->getXPos()) + ":");
	s.append(std::to_string(game->getController()->getTarget()->getYPos()) + ":");
	s.append(std::to_string(game->getController()->getTarget()->getZPos()));

	al_draw_text(game->getManager()->getFont("sans"), al_map_rgb(255,0,0), 10, 10, ALLEGRO_ALIGN_LEFT, s.c_str() ); 
	al_draw_text(game->getManager()->getFont("sans"), al_map_rgb(255,0,0), 10, 20, ALLEGRO_ALIGN_LEFT, std::string("Pitch: " + std::to_string(int(game->getController()->getTarget()->getXRot()  ) )).c_str()); 
	al_draw_text(game->getManager()->getFont("sans"), al_map_rgb(255,0,0), 10, 30, ALLEGRO_ALIGN_LEFT, std::string("Yaw: "   + std::to_string(int(game->getController()->getTarget()->getYRot()  ) )).c_str()); 
}
