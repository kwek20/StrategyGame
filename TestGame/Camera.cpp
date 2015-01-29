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
	setPitch(START_PITCH);
	setYaw(START_YAW);
}

Camera::Camera(float x, float z, float y, float pitch, float yaw){
	setX(x);
	setY(y);
	setZ(z);
	setPitch(pitch);
	setYaw(yaw);
}


Camera::~Camera(void){

}

void Camera::addPitch(float pitch){
	setPitch(getPitch() + pitch);
}

void Camera::setPitch(float pitch){
	_pitch = clamp<float>(pitch, PITCH_MIN, PITCH_MAX);
}

void Camera::addYaw(float yaw){
	setYaw(clamp<float>(getYaw() + yaw, YAW_MIN, YAW_MAX, true));
}

void Camera::setYaw(float yaw){
	_yaw = clamp<float>(yaw, YAW_MIN, YAW_MAX);
}

template <typename T>
T Camera::clamp(T val, T min, T max, bool goNegative){
	return val < min ? (goNegative ? fmod(val, max) + max : min) : (val > max ? (goNegative ? min + fmod(val, max) : max) : val);
}

void Camera::move(MOVE_DIRECTION direction){
	std::cout << "Direction: " << direction << "\n";

	float x = (direction == UP || direction == UP_RIGHT || direction == LEFT_UP || direction == DOWN || direction == DOWN_LEFT || direction == RIGHT_DOWN) ? ONE_MOVE : 0;
	float z = (direction == RIGHT || direction == UP_RIGHT || direction == RIGHT_DOWN || direction == LEFT || direction == LEFT_UP || direction == DOWN_LEFT) ? ONE_MOVE : 0;
	float rad = fmod(fabs(getYaw()) * ALLEGRO_PI / 180, 180);
	x = SPEED * sin(rad) * x;
	z = SPEED * cos(rad) * x;

	if (direction == DOWN || direction == DOWN_LEFT || direction == RIGHT_DOWN) x = -x;
	if (direction == LEFT || direction == LEFT_UP || direction == DOWN_LEFT) z = -z;

	std::cout << "values: " << getYaw() << " " << rad << " " << x << " " << z << "\n";
	addX(x);
	addZ(z);
}

void Camera::camera_2D_setup(ALLEGRO_DISPLAY* display){
	/*glMatrixMode(GL_PROJECTION);
	glLoadIdentity();*/
	glOrtho(0, al_get_display_width(display), al_get_display_height(display), 0.0, 0.0, 1.0);
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
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
