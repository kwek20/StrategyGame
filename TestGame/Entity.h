#ifndef __ENTITY_H_INCLUDED__
#define __ENTITY_H_INCLUDED__

#pragma once

#include "Vec3.hpp"       // Include our custom Vec3 class

class Entity
{
public:
	Entity(void);
	Entity(Vec3<double> position);
	Entity(Vec3<double> position, Vec3<double> rotation);

	virtual ~Entity(void);
	void unPossess();
	Vec3<double> getPosition(){return position;}
	Vec3<double> getRotation(){return rotation;}
	double getMovementSpeedFactor(){return movementSpeedFactor;}

protected:
	// Entity position
	Vec3<double> position;

	// Entity rotation
	Vec3<double> rotation;

	// Entity movement speed. When we call the move() function on it, it moves using these speeds
	Vec3<double> speed;

    double movementSpeedFactor; // Controls how fast the camera moves
private:
	void load(Vec3<double> position, Vec3<double> rotation);
};

#endif