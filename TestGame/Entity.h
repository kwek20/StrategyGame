#ifndef __ENTITY_H_INCLUDED__
#define __ENTITY_H_INCLUDED__

#pragma once

#include "Vec3.hpp"       // Include our custom Vec3 class

class Entity
{
public:
	Entity(void);
	virtual ~Entity(void);

protected:
	// Entity position
	Vec3<double> position;

	// Entity rotation
	Vec3<double> rotation;

	// Entity movement speed. When we call the move() function on it, it moves using these speeds
	Vec3<double> speed;
};

#endif