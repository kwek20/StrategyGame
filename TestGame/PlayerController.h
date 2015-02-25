#ifndef __PLAYER_CONTROLLER_H_INCLUDED__
#define __PLAYER_CONTROLLER_H_INCLUDED__

#pragma once

#include "Entity.h"
#include "Vec3.hpp"
#include <allegro5\allegro.h>

#define TO_RADS ALLEGRO_PI / 180.0;

class PlayerController
{
public:
	PlayerController(void);
	PlayerController(Entity *target);
	virtual ~PlayerController(void);

	// Holding any keys down?
	bool holdingForward;
	bool holdingBackward;
	bool holdingLeftStrafe;
	bool holdingRightStrafe;

	void setTarget(Entity *newTarget);
	bool hasTarget(){return target != NULL;}
	Entity *getTarget(){return target;}

	Vec3<double> getLocation();
	Vec3<double> getRotation();

	void handleMouseMove(int mouseX, int mouseY);
	void move(double deltaTime);

	// Setters to allow for change of vertical (pitch) and horizontal (yaw) mouse movement sensitivity
	float getPitchSensitivity()            { return pitchSensitivity;  }
	void  setPitchSensitivity(float value) { pitchSensitivity = value; }
	float getYawSensitivity()              { return yawSensitivity;    }
	void  setYawSensitivity(float value)   { yawSensitivity   = value; }
private:
	Entity *target;

	const double toRads(const double &theAngleInDegrees) const;
	void load();

	double pitchSensitivity;    // Controls how sensitive mouse movements affect looking up and down
    double yawSensitivity;      // Controls how sensitive mouse movements affect looking left and right
};

#endif