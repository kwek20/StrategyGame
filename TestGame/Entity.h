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

	virtual void draw();
	virtual void move(float deltaTime);
	virtual void update(float deltaTime);

	virtual void moveAdd(Vec3<double> newPos);
	virtual void rotateAdd(Vec3<double> newRot);

	virtual void moveTo(Vec3<double> newPos);
	virtual void rotateTo(Vec3<double> newRot);

	virtual const std::string getName(){return "Entity";};

	bool isInAir(){return inAir;}
	void setInAir(bool air){inAir = air;}

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

	Vec3<double> getVelocity(){return velocity;}
	void setVelocity(Vec3<double> newVelocity){velocity = newVelocity;}

	bool hasVelocity(){return velocity.getX() != 0 || velocity.getY() != 0 || velocity.getZ() != 0;}

	void getPixelLocation(int *x, int *y);

	//dump to stdout
	void dump();
protected:
	// Entity position
	Vec3<double> position;

	// Entity rotation
	Vec3<double> rotation;

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