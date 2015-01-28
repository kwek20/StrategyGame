#ifndef __CAMERA_H_INCLUDED__
#define __CAMERA_H_INCLUDED__

struct ALLEGRO_DISPLAY;

#pragma once
class Camera
{
private:
	float _posX, _posY, _posZ, _angle, _rotation;
public:
	Camera(void);
	virtual ~Camera(void);

	void setAngle(float angle);
	void addAngle(float angle);
	float getAngle(){return _angle;}

	void setRotation(float rotation);
	void addRotation(float rotation);
	float getRotation(){return _rotation;}

	float getX(){return _posX;}
	float getY(){return _posY;}
	float getZ(){return _posZ;}

	void camera_2D_setup(ALLEGRO_DISPLAY* display);
	void camera_3D_setup(ALLEGRO_DISPLAY* display);
};

#endif
