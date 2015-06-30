#ifndef __ENTITY_H_INCLUDED__
#define __ENTITY_H_INCLUDED__

#pragma once

#include "Vec.hpp"       // Include our custom Vec3 class
#include <iostream>

#include "Object.h"

class Entity abstract : public Object
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

	virtual void draw();
	virtual void draw2D();
	virtual void move(float deltaTime);
	virtual void update(float deltaTime);

	virtual const std::string getName(){return "Entity";};

	bool isInAir(){return inAir;}
	void setInAir(bool air){inAir = air;}

	double getMovementSpeedFactor(){return movementSpeedFactor;}

	Vec3<double> getVelocity(){return velocity;}
	void setVelocity(Vec3<double> newVelocity){velocity = newVelocity;}

	bool hasVelocity(){return velocity.getX() != 0 || velocity.getY() != 0 || velocity.getZ() != 0;}

	void getPixelLocation(int *x, int *y);
protected:
	// Entity velocity, update() functions uses this to calculate movement per deltatime
	Vec3<double> velocity;

	double gravity;
	double friction;
	double density;
    double movementSpeedFactor; // Controls how fast the camera moves
private:
	void load(Vec3<double> position, Vec3<double> rotation);

	bool inAir;
};

#endif