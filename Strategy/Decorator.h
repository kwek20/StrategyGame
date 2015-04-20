#ifndef __DECORATOR_H_INCLUDED__
#define __DECORATOR_H_INCLUDED__

#pragma once

#include <vector>
#include "noiseutils.h"

class Decorator
{
public:
	Decorator(void);
	~Decorator(void);

	void getColorAt(float height, unsigned char *r, unsigned char *g, unsigned char *b);
	void addColor(float start, utils::Color color);

private:
	noise::utils::GradientColor color;
};

#endif