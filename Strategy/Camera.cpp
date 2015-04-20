#include "Camera.h"
 
Camera::Camera(float theWindowWidth, float theWindowHeight, PlayerController target){
	windowWidth  = theWindowWidth;
	windowHeight = theWindowHeight;
 
	// Calculate the middle of the window
	windowMidX = windowWidth  / 2.0f;
	windowMidY = windowHeight / 2.0f;

	this->target = target;

	LightAmbient[0] = 30.5f;
	LightAmbient[1] = 0.5f;
	LightAmbient[2] = 0.5f;
	LightAmbient[3] = 1.0f;

	LightDiffuse[0] = 1.0f;
	LightDiffuse[1] = 1.0f;
	LightDiffuse[2] = 1.0f;
	LightDiffuse[3] = 1.0f;

	LightPosition[0] = 0.0f;
	LightPosition[1] = 20.0f;
	LightPosition[2] = 100.0f;
	LightPosition[3] = 1.0f;
}
 
Camera::~Camera(){
	// Nothing to do here - we don't need to free memory as all member variables
	// were declared on the stack.
}

void Camera::camera_2D_setup(ALLEGRO_DISPLAY* display){
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, al_get_display_width(display), al_get_display_height(display), 0.0, 0.0, 1.0);
	
	glMatrixMode(GL_MODELVIEW);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
}

void Camera::camera_3D_setup(ALLEGRO_DISPLAY* display){
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(35, (GLdouble)al_get_display_width(display) / (GLdouble)al_get_display_height(display), 1.0, 2000.0);

	glRotatef(target.getTarget()->getXRot(), 1, 0, 0);
	glRotatef(target.getTarget()->getYRot(), 0, 1, 0);

	glTranslatef(-target.getTarget()->getXPos(), -target.getTarget()->getYPos(), -target.getTarget()->getZPos());

	//glEnable(GL_TEXTURE_2D);
	
	glShadeModel(GL_SMOOTH);		 // Enables Smooth Shading
	glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
	glClearDepth(1.0f);				// Depth Buffer Setup
}
