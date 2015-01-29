#ifndef __CAMERA_H_INCLUDED__
#define __CAMERA_H_INCLUDED__

#pragma once

#include "Map.h"
#include "Status.h"

#define MAX_HEIGHT 500
#define START_ANGLE 45
#define ONE_MOVE 1
#define SPEED 1.2

struct ALLEGRO_DISPLAY;

class Camera
{
private:
	float _posX, _posY, _posZ, _pitch, _yaw;
public:
	Camera(Map *map);
	Camera(float x=0, float z=0, float y=MAX_HEIGHT, float pitch=START_ANGLE);
	virtual ~Camera(void);

	//
	void setPitch(float pitch);
	void addPitch(float pitch);
	float getPitch(){return _pitch;}

	void setYaw(float yaw);
	void addYaw(float yaw);
	float getYaw(){return _yaw;}

	float getX(){return _posX;}
	float getY(){return _posY;}
	float getZ(){return _posZ;}

	void setX(float x){_posX = x;}
	void setY(float y){_posY = y;}
	void setZ(float z){_posZ = z;}

	void addX(float x){_posX += x;}
	void addY(float y){_posY += y;}
	void addZ(float z){_posZ += z;}

	void move(MOVE_DIRECTION direction);

	void camera_2D_setup(ALLEGRO_DISPLAY* display);
	void camera_3D_setup(ALLEGRO_DISPLAY* display);
};

#endif
