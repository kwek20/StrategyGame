#ifndef __MATH_H_INCLUDED__
#define __MATH_H_INCLUDED__

#include <cmath>
#include <iostream>

#include <allegro5\allegro.h>

#include "Vec3.hpp"

#define TO_RADS ALLEGRO_PI / 180.0;
#define TO_DEGREE 180.0 / ALLEGRO_PI;

//Calculate the angle between 2 height points
float calculateAngle(float y1, float y2, float adjecent){
	float lowY = y1 < y2 ? y1 : y2; 
	float highY = lowY == y1 ? y2 : y1;

	highY-=lowY;
	return atanf(highY/adjecent);
}

//returns -1 when the point is not on the triangle
float calcY(Vec3<GLfloat> p1, Vec3<GLfloat> p2, Vec3<GLfloat> p3, float x, float z){
	float det = (p2.getZ() - p3.getZ()) * (p1.getX() - p3.getX()) + (p3.getX() - p2.getX()) * (p1.getZ() - p3.getZ());
	float l1 = ((p2.getZ() - p3.getZ()) * (x - p3.getX()) + (p3.getX() - p2.getX()) * (z - p3.getZ())) / det;
	if (l1 < 0 || l1 > 1) return -1;

	float l2 = ((p3.getZ() - p1.getZ()) * (x - p3.getX()) + (p1.getX() - p3.getX()) * (z - p3.getZ())) / det;
	if (l2 < 0 || l2 > 1) return -1;

	float l3 = 1.0f - l1 - l2;
	return l1 * p1.getY() + l2 * p2.getY() + l3 * p3.getY();
}

float radToDegree(float rad){return rad * TO_DEGREE;}
float degreeToRad(float degree){return degree * TO_RADS;}

#endif
