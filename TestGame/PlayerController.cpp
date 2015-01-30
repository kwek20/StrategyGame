#include "PlayerController.h"


PlayerController::PlayerController(void){
	load();
}

PlayerController::PlayerController(Entity *target){
	load();
	setTarget(target);
}

void PlayerController::load(){
	pitchSensitivity = 0.2; // How sensitive mouse movements affect looking up and down
	yawSensitivity   = 0.2; // How sensitive mouse movements affect looking left and right
 
	// To begin with, we aren't holding down any keys
	holdingForward     = false;
	holdingBackward    = false;
	holdingLeftStrafe  = false;
	holdingRightStrafe = false;
}

PlayerController::~PlayerController(void){
	//nothing here, were on the stack :D
}

void PlayerController::setTarget(Entity *newTarget){
	if (hasTarget()) target->unPossess();
	target  = newTarget;
}

Vec3<double> PlayerController::getLocation(){
	if (hasTarget()) return target->getPosition();
	return Vec3<double>(0,0,0);
}

Vec3<double> PlayerController::getRotation(){
	if (hasTarget()) return target->getRotation();
	return Vec3<double>(0,0,0);
}

// Function to convert degrees to radians
const double PlayerController::toRads(const double &theAngleInDegrees) const{
	return theAngleInDegrees * TO_RADS;
}

// Function to deal with mouse position changes
void PlayerController::handleMouseMove(int mouseX, int mouseY){
	// Calculate our horizontal and vertical mouse movement from middle of the window
	double horizMovement = mouseX * yawSensitivity;
	double vertMovement  = mouseY * pitchSensitivity;
 
	//std::cout << "Mid window values: " << windowMidX << "\t" << windowMidY << std::endl;
	//std::cout << "Mouse values     : " << mouseX << "\t" << mouseY << std::endl;
	//std::cout << horizMovement << "\t" << vertMovement << std::endl << std::endl;
 
	// Apply the mouse movement to our rotation vector. The vertical (look up and down)
	// movement is applied on the X axis, and the horizontal (look left and right)
	// movement is applied on the Y Axis
	getRotation().addX(vertMovement);
	getRotation().addY(horizMovement);
 
	// Limit looking up to vertically up
	if (getRotation().getX() < -90){
		getRotation().setX(-90);
	}
 
	// Limit looking down to vertically down
	if (getRotation().getX() > 90){
		getRotation().setX(90);
	}
 
	// Looking left and right - keep angles in the range 0.0 to 360.0
	// 0 degrees is looking directly down the negative Z axis "North", 90 degrees is "East", 180 degrees is "South", 270 degrees is "West"
	// We can also do this so that our 360 degrees goes -180 through +180 and it works the same, but it's probably best to keep our
	// range to 0 through 360 instead of -180 through +180.
	if (getRotation().getY() < 0){
		getRotation().addY(360);
	}
	if (getRotation().getY() > 360){
		getRotation().addY(-360);
	}
}
 
// Function to calculate which direction we need to move the camera and by what amount
void PlayerController::move(double deltaTime){
	// Vector to break up our movement into components along the X, Y and Z axis
	Vec3<double> movement;
 
	// Get the sine and cosine of our X and Y axis rotation
	double sinXRot = sin( toRads( getRotation().getX() ) );
	double cosXRot = cos( toRads( getRotation().getX() ) );
 
	double sinYRot = sin( toRads( getRotation().getY() ) );
	double cosYRot = cos( toRads( getRotation().getY() ) );
 
	double pitchLimitFactor = cosXRot; // This cancels out moving on the Z axis when we're looking up or down
 
	if (holdingForward){
		movement.addX(sinYRot * pitchLimitFactor);
		movement.addY(-sinXRot);
		movement.addZ(-cosYRot * pitchLimitFactor);
	}
 
	if (holdingBackward){
		movement.addX(-sinYRot * pitchLimitFactor);
		movement.addY(sinXRot);
		movement.addZ(cosYRot * pitchLimitFactor);
	}
 
	if (holdingLeftStrafe){
		movement.addX(-cosYRot);
		movement.addZ(-sinYRot);
	}
 
	if (holdingRightStrafe){
		movement.addX(cosYRot);
		movement.addZ(sinYRot);
	}
 
	// Normalise our movement vector
	movement.normalise();
 
	// Calculate our value to keep the movement the same speed regardless of the framerate...
	double framerateIndependentFactor = getTarget()->getMovementSpeedFactor() * deltaTime;
 
	// .. and then apply it to our movement vector.
	movement *= framerateIndependentFactor;
 
	// Finally, apply the movement to our position
	getLocation() += movement;
}
