#include "Decorator.h"

#include <iostream>

Decorator::Decorator(void){
	color.Clear();
	addColor ( 0, utils::Color ( 32, 70,   80, 255)); // grass
	addColor ( 15, utils::Color ( 32, 160,   0, 255)); // grass
	addColor ( 50, utils::Color (224, 224,   0, 255)); // dirt
	addColor ( 100, utils::Color (128, 128, 128, 255)); // rock
	addColor ( 150.00, utils::Color (255, 255, 255, 255)); // snow*/
}


Decorator::~Decorator(void)
{
}

void Decorator::addColor(float start, utils::Color color){
	this->color.AddGradientPoint (start, color);
}

void Decorator::getColorAt(float height, unsigned char *r, unsigned char *g, unsigned char *b){
	noise::utils::Color color = this->color.GetColor(height);
	*r = color.red;
	*g = color.green;
	*b = color.blue;
}