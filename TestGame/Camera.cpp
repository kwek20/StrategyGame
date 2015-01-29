#include "Camera.h"
 
const double Camera::TO_RADS = ALLEGRO_PI / 180.0; // The value of 1 degree in radians
 
Camera::Camera(float theWindowWidth, float theWindowHeight)
{
	initCamera();
 
	windowWidth  = theWindowWidth;
	windowHeight = theWindowHeight;
 
	// Calculate the middle of the window
	windowMidX = windowWidth  / 2.0f;
	windowMidY = windowHeight / 2.0f;
}
 
Camera::~Camera()
{
	// Nothing to do here - we don't need to free memory as all member variables
	// were declared on the stack.
}
 
void Camera::initCamera()
{
	// Set position, rotation and speed values to zero
	position.zero();
	rotation.zero();
	speed.zero();
 
	// How fast we move (higher values mean we move and strafe faster)
	movementSpeedFactor = 100.0;
 
	pitchSensitivity = 0.2; // How sensitive mouse movements affect looking up and down
	yawSensitivity   = 0.2; // How sensitive mouse movements affect looking left and right
 
	// To begin with, we aren't holding down any keys
	holdingForward     = false;
	holdingBackward    = false;
	holdingLeftStrafe  = false;
	holdingRightStrafe = false;
}
 
// Function to convert degrees to radians
const double Camera::toRads(const double &theAngleInDegrees) const
{
	return theAngleInDegrees * TO_RADS;
}
 
// Function to deal with mouse position changes
void Camera::handleMouseMove(int mouseX, int mouseY)
{
	// Calculate our horizontal and vertical mouse movement from middle of the window
	double horizMovement = (mouseX - windowMidX+1) * yawSensitivity;
	double vertMovement  = (mouseY - windowMidY) * pitchSensitivity;
 
	std::cout << "Mid window values: " << windowMidX << "\t" << windowMidY << std::endl;
	std::cout << "Mouse values     : " << mouseX << "\t" << mouseY << std::endl;
	std::cout << horizMovement << "\t" << vertMovement << std::endl << std::endl;
 
	// Apply the mouse movement to our rotation vector. The vertical (look up and down)
	// movement is applied on the X axis, and the horizontal (look left and right)
	// movement is applied on the Y Axis
	rotation.addX(vertMovement);
	rotation.addY(horizMovement);
 
	// Limit loking up to vertically up
	if (rotation.getX() < -90)
	{
		rotation.setX(-90);
	}
 
	// Limit looking down to vertically down
	if (rotation.getX() > 90)
	{
		rotation.setX(90);
	}
 
	// If you prefer to keep the angles in the range -180 to +180 use this code
	// and comment out the 0 to 360 code below.
	//
	// Looking left and right. Keep the angles in the range -180.0f (anticlockwise turn looking behind) to 180.0f (clockwise turn looking behind)
	/*if (yRot < -180.0f)
	{
	    yRot += 360.0f;
	}

	if (yRot > 180.0f)
	{
	    yRot -= 360.0f;
	}*/
 
	// Looking left and right - keep angles in the range 0.0 to 360.0
	// 0 degrees is looking directly down the negative Z axis "North", 90 degrees is "East", 180 degrees is "South", 270 degrees is "West"
	// We can also do this so that our 360 degrees goes -180 through +180 and it works the same, but it's probably best to keep our
	// range to 0 through 360 instead of -180 through +180.
	if (rotation.getY() < 0)
	{
		rotation.addY(360);
	}
	if (rotation.getY() > 360)
	{
		rotation.addY(-360);
	}
 
	// Reset the mouse position to the centre of the window each frame
}
 
// Function to calculate which direction we need to move the camera and by what amount
void Camera::move(double deltaTime)
{
	// Vector to break up our movement into components along the X, Y and Z axis
	Vec3<double> movement;
 
	// Get the sine and cosine of our X and Y axis rotation
	double sinXRot = sin( toRads( rotation.getX() ) );
	double cosXRot = cos( toRads( rotation.getX() ) );
 
	double sinYRot = sin( toRads( rotation.getY() ) );
	double cosYRot = cos( toRads( rotation.getY() ) );
 
	double pitchLimitFactor = cosXRot; // This cancels out moving on the Z axis when we're looking up or down
 
	if (holdingForward)
	{
		movement.addX(sinYRot * pitchLimitFactor);
		movement.addY(-sinXRot);
		movement.addZ(-cosYRot * pitchLimitFactor);
	}
 
	if (holdingBackward)
	{
		movement.addX(-sinYRot * pitchLimitFactor);
		movement.addY(sinXRot);
		movement.addZ(cosYRot * pitchLimitFactor);
	}
 
	if (holdingLeftStrafe)
	{
		movement.addX(-cosYRot);
		movement.addZ(-sinYRot);
	}
 
	if (holdingRightStrafe)
	{
		movement.addX(cosYRot);
		movement.addZ(sinYRot);
	}
 
	// Normalise our movement vector
	movement.normalise();
 
	// Calculate our value to keep the movement the same speed regardless of the framerate...
	double framerateIndependentFactor = movementSpeedFactor * deltaTime;
 
	// .. and then apply it to our movement vector.
	movement *= framerateIndependentFactor;
 
	// Finally, apply the movement to our position
	position += movement;
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
