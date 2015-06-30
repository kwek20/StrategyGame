#ifndef __OBJECT_H_INCLUDED__
#define __OBJECT_H_INCLUDED__

#pragma once

#include "Vec.hpp"

class Object abstract
{
public:
	Object(void);

	Object(double x, double y, double z);
	Object(double x, double y, double z, double xr, double yr, double zr);

	Object(Vec3<double> position);
	Object(Vec3<double> position, Vec3<double> rotation);

	virtual void moveAdd(Vec3<double> newPos);
	virtual void rotateAdd(Vec3<double> newRot);

	virtual void moveTo(Vec3<double> newPos);
	virtual void rotateTo(Vec3<double> newRot);

	// Position getters
	Vec3<double> getPosition() const { return position; }
	double getXPos()           const { return position.getX(); }
	double getYPos()           const { return position.getY(); }
	double getZPos()           const { return position.getZ(); }

	// Rotation getters
	Vec3<double> getRotation() const { return rotation; }
	double getXRot()           const { return rotation.getX(); }
	double getYRot()           const { return rotation.getY(); }
	double getZRot()           const { return rotation.getZ(); }

	~Object(void);

	virtual void dump();
	virtual void draw() = 0;

	virtual const std::string getName()=0;
protected:
	// Entity position
	Vec3<double> position;

	// Entity rotation
	Vec3<double> rotation;

	/**
	  * Dont forget to call super!
	  */
	virtual void load(Vec3<double> position, Vec3<double> rotation);
private:
	
};

#endif