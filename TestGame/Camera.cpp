#include "Camera.h"

#include <allegro5\allegro.h>
#include <allegro5\allegro_opengl.h>
#include <allegro5\opengl\gl_ext.h>
#include <gl\glu.h>

#include <math.h>

Camera::Camera(void){
	_posX = 0;
	_posY = 0;
	_posZ = 0;
	_angle = 0;
}


Camera::~Camera(void){

}

void Camera::addAngle(float angle){
	float newAngle = getAngle() + angle;
	setAngle(
		newAngle > 360 ? -360 + fmod(newAngle, 360) : 
		newAngle < -360 ? fmod(newAngle, 360) : 
		newAngle);
}

void Camera::setAngle(float angle){
	_angle = angle < -360.0 ? -360.0 : angle > 360.0 ? 360.0 : angle;
}

void Camera::addRotation(float rotation){
	float newRotation = getRotation() + rotation;
	setRotation(
		newRotation > 360 ? -360 + fmod(newRotation, 360) : 
		newRotation < -360 ? fmod(newRotation, 360) : 
		newRotation);
}

void Camera::setRotation(float rotation){
	_rotation = rotation < -360.0 ? -360.0 : rotation > 360.0 ? 360.0 : rotation;
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

	glRotatef(getAngle(), 1, 0, 0);
	glRotatef(getRotation(), 0, 1, 0);
	glTranslatef(0.0f, -450.0f, -550.0f);
	glEnable(GL_DEPTH_TEST);
}
