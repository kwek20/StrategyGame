#ifndef __CAMERA_H_INCLUDED__
#define __CAMERA_H_INCLUDED__
 
#include <iostream>
#include <math.h>         // Used only for sin() and cos() functions
#include <allegro5\allegro.h>
#include <allegro5\allegro_opengl.h>
#include <gl\GLU.h>
 
#include "Vec.hpp"       // Include our custom Vec3 class
#include "PlayerController.h" 

/*
 * Camera class from http://r3dux.org/2012/12/a-c-camera-class-for-simple-opengl-fps-controls/
 *
 *
*/
class Camera{
private:
	GLfloat LightAmbient[4];
	GLfloat LightDiffuse[4];
	GLfloat LightPosition[4];

    protected:
        // Window size in pixels and where the midpoint of it falls
        int windowWidth;
        int windowHeight;
        int windowMidX;
        int windowMidY;
     
		//we're following the controller
		PlayerController target;
    public:
        // Constructor
        Camera(float windowWidth, float windowHeight, PlayerController target);

        // Destructor
        ~Camera();

		void camera_2D_setup(ALLEGRO_DISPLAY* display);
		void camera_3D_setup(ALLEGRO_DISPLAY* display);
};
 
#endif // CAMERA_H