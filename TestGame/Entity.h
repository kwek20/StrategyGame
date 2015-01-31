#ifndef __ENTITY_H_INCLUDED__
#define __ENTITY_H_INCLUDED__

#pragma once

#include "Vec3.hpp"       // Include our custom Vec3 class
#include <iostream>

class Entity abstract
{
public:
	virtual int getMaxPitch() = 0;
	virtual int getMinPitch() = 0;
	virtual int getMaxYaw() = 0;
	virtual int getMinYaw() = 0;

	Entity(void);
	Entity(double x, double y, double z);
	Entity(double x, double y, double z, double xr, double yr, double zr);

	Entity(Vec3<double> position);
	Entity(Vec3<double> position, Vec3<double> rotation);

	virtual ~Entity(void);
	void unPossess();
	void draw();

	virtual void moveAdd(Vec3<double> newPos);
	virtual void rotateAdd(Vec3<double> newRot);

	virtual void moveTo(Vec3<double> newPos);
	virtual void rotateTo(Vec3<double> newRot);

	virtual const std::string getName(){return "Entity";};

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

	//dump to stdout
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