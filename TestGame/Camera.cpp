#include "Camera.h"

#include <allegro5\allegro.h>
#include <allegro5\allegro_opengl.h>
#include <allegro5\opengl\gl_ext.h>
#include <gl\glu.h>

#include <math.h>

#include <iostream>

Camera::Camera(Map *map){
	setX(map->getX());
	setY(MAX_HEIGHT);
	setZ(map->getZ());
	setPitch(START_ANGLE);
}

Camera::Camera(float x, float z, float y, float pitch){
	setX(x);
	setY(y);
	setZ(z);
	setPitch(pitch);
}


Camera::~Camera(void){

}

void Camera::addPitch(float pitch){
	float newPitch = getPitch() + pitch;
	setPitch(
		newPitch > 360 ? -360 + fmod(newPitch, 360) : 
		newPitch < -360 ? fmod(newPitch, 360) : 
		newPitch);
}

void Camera::setPitch(float pitch){
	_pitch = pitch < -360.0 ? -360.0 : pitch > 360.0 ? 360.0 : pitch;
}

void Camera::addYaw(float yaw){
	float newRotation = getYaw() + yaw;
	setYaw(
		newRotation > 360 ? -360 + fmod(newRotation, 360) : 
		newRotation < -360 ? fmod(newRotation, 360) : 
		newRotation);
}

void Camera::setYaw(float yaw){
	_yaw = yaw < -360.0 ? -360.0 : yaw > 360.0 ? 360.0 : yaw;
}

void Camera::move(MOVE_DIRECTION direction){
	std::cout << "Direction: " << direction << "\n";
	/*ALLEGRO_TRANSFORM trans;
	al_identity_transform(&trans);
	//if somethign with up, one move up, somethign with down, 1 down else 0
	trans.m[0][3] = (direction == UP || direction == UP_RIGHT || direction == LEFT_UP) ? ONE_MOVE : (direction == DOWN || direction == DOWN_LEFT || direction == RIGHT_DOWN) ? -ONE_MOVE : 0;
	//dont care about y
	trans.m[2][3] = (direction == RIGHT || direction == UP_RIGHT || direction == RIGHT_DOWN) ? ONE_MOVE : (direction == LEFT || direction == LEFT_UP || direction == DOWN_LEFT) ? -ONE_MOVE : 0;
	//al_rotate_transform_3d(&trans, getX(), getY(), getZ(), getRotation());
	al_horizontal_shear_transform(&trans, getRotation());
	std::cout << trans.m[0][0] << " " << trans.m[0][1] << " " << trans.m[0][2] << " " << trans.m[0][3] << "\n";
	std::cout << trans.m[1][0] << " " << trans.m[1][1] << " " << trans.m[1][2] << " " << trans.m[1][3] << "\n";
	std::cout << trans.m[2][0] << " " << trans.m[2][1] << " " << trans.m[2][2] << " " << trans.m[2][3] << "\n";
	std::cout << trans.m[3][0] << " " << trans.m[3][1] << " " << trans.m[3][2] << " " << trans.m[3][3] << "\n";
	std::cout << getX() << " " << getY() << " " << getZ() << " " << getRotation() << "\n";
	std::cout << "---------------------------------------------\n";*/

	float x = (direction == UP || direction == UP_RIGHT || direction == LEFT_UP || direction == DOWN || direction == DOWN_LEFT || direction == RIGHT_DOWN) ? ONE_MOVE : 0;
	float z = (direction == RIGHT || direction == UP_RIGHT || direction == RIGHT_DOWN || direction == LEFT || direction == LEFT_UP || direction == DOWN_LEFT) ? ONE_MOVE : 0;
	x = SPEED * cos(fabs(getYaw()) * ALLEGRO_PI / 180) * x;
	z = SPEED * sin(fabs(getYaw()) * ALLEGRO_PI / 180) * z;

	if (direction == UP || direction == UP_RIGHT || direction == LEFT_UP) x = -1;
	if (direction == RIGHT || direction == UP_RIGHT || direction == RIGHT_DOWN) z = -1;

	std::cout << "values: " << getYaw() << " " << x << " " << z << "\n";
	addX(x);
	addZ(z);
}

void Camera::camera_2D_setup(ALLEGRO_DISPLAY* display){
	/*glMatrixMode(GL_PROJECTION);
	glLoadIdentity();*/
	glOrtho(0, al_get_display_width(display), al_get_display_height(display), 0.0, 0.0, 1.0);
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
}

void Camera::camera_3D_setup(ALLEGRO_DISPLAY* display){
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(35, (GLdouble)al_get_display_width(display) / (GLdouble)al_get_display_height(display), 1.0, 2000.0);


	glRotatef(getPitch(), 1, 0, 0);
	glRotatef(getYaw(), 0, 1, 0);

	glTranslatef(-getX(), -getY(), -getZ());
	glEnable(GL_DEPTH_TEST);
}
