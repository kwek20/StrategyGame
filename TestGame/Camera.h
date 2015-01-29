#ifndef __CAMERA_H_INCLUDED__
#define __CAMERA_H_INCLUDED__

#pragma once

#include "Map.h"
#include "Status.h"

#define MAX_HEIGHT 500

#define ONE_MOVE 1
#define SPEED 1.2

#define YAW_MIN 0
#define YAW_MAX 360
#define PITCH_MIN 15
#define PITCH_MAX 90

#define START_PITCH PITCH_MIN+PITCH_MAX/2
#define START_YAW YAW_MIN+YAW_MAX/2

struct ALLEGRO_DISPLAY;

class Camera
{
private:
	float _posX, _posY, _posZ, _pitch, _yaw;
public:
	Camera(Map *map);
	Camera(float x=0, float z=0, float y=MAX_HEIGHT, float pitch=START_PITCH, float yaw=START_YAW);
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

	template <typename T>
	T clamp(T val, T min, T max, bool goNegative=false);

	void camera_2D_setup(ALLEGRO_DISPLAY* display);
	void camera_3D_setup(ALLEGRO_DISPLAY* display);
};

#endif
