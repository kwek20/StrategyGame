#ifndef __ENTITY_H_INCLUDED__
#define __ENTITY_H_INCLUDED__

#pragma once

#include "Vec3.hpp"       // Include our custom Vec3 class
#include <iostream>

class Entity abstract
{
public:
	Entity(void);
	Entity(Vec3<double> position);
	Entity(Vec3<double> position, Vec3<double> rotation);

	virtual ~Entity(void);
	void unPossess();

	virtual const std::string getName() = 0;

	// Position getters
	Vec3<double> getPosition() const { return position;        }
	double getXPos()           const { return position.getX(); }
	double getYPos()           const { return position.getY(); }
	double getZPos()           const { return position.getZ(); }

	// Rotation getters
	Vec3<double> getRotation() const { return rotation;        }
	double getXRot()           const { return rotation.getX(); }
	double getYRot()           const { return rotation.getY(); }
	double getZRot()           const { return rotation.getZ(); }

	double getMovementSpeedFactor(){return movementSpeedFactor;}
	void dump();
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