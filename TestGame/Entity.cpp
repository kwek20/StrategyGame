#include "Entity.h"
#include <typeinfo>
#include <stdio.h>

//needed for rendering default object
#include <allegro5\allegro.h>
#include <allegro5\allegro_opengl.h>

#include <gl\GLU.h>

#include <allegro5\allegro_font.h>

Entity::Entity(void){
	load(Vec3<double>(0,0,0), Vec3<double>(0,0,0));
}

Entity::Entity(double x, double y, double z){
	load(Vec3<double>(x,y,z), Vec3<double>(0,0,0));
}

Entity::Entity(double x, double y, double z, double xr, double yr, double zr){
	load(Vec3<double>(x,y,z), Vec3<double>(xr,yr,zr));
}

Entity::Entity(Vec3<double> position){
	load(position, Vec3<double>(0,0,0));
}

Entity::Entity(Vec3<double> position, Vec3<double> rotation){
	load(position, rotation);
}

Entity::~Entity(void){

}

void Entity::unPossess(){

}

void Entity::moveAdd(Vec3<double> newPos){
	moveTo(position + newPos);
}

void Entity::moveTo(Vec3<double> newPos){
	position = newPos;
}

void Entity::rotateAdd(Vec3<double> newRot){
	rotateTo(rotation + newRot);
}

void Entity::rotateTo(Vec3<double> newRot){
	rotation = newRot;
}

void Entity::load(Vec3<double> position, Vec3<double> rotation){
	// Set position, rotation 

	this->position = position;
	this->rotation = rotation;

	// How fast we move (higher values mean we move and strafe faster)
	movementSpeedFactor = 100.0;

	//downward force! :D
	gravity = 9.81 * 6;

	//slight friction
	friction = 1.1;

	density = 1.293;
}

void Entity::draw(){
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	GLUquadric *g = gluNewQuadric();
	gluQuadricNormals(g, GLU_SMOOTH);

	glPushMatrix();
	glTranslatef(getXPos(), getYPos(), getZPos());
	gluSphere(g, 1,10, 10);

	gluDeleteQuadric(g);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	int x, y;
	getPixelLocation(&x, &y);
	al_draw_text(al_create_builtin_font(), al_map_rgb(255,0,0), x, y, ALLEGRO_ALIGN_CENTER, getName().c_str() ); 

	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
}

void Entity::getPixelLocation(int *x, int *y){
	GLdouble modelMatrix[16];
	GLdouble projMatrix[16];
	GLint viewport[4];

	GLdouble px;
	GLdouble py;
	GLdouble pz;

	glGetDoublev(GL_MODELVIEW_MATRIX, modelMatrix);
	glGetDoublev(GL_PROJECTION_MATRIX, projMatrix);
	glGetIntegerv(GL_VIEWPORT, viewport);
	int result = gluProject(getXPos(), getYPos(), getZPos(), modelMatrix, projMatrix, viewport, &px, &py, &pz);
	*x = (int)px;
	*y = viewport[1] + viewport[3] - (int)py;
}

void Entity::move(float deltaTime){
	//y velocity = m/s
	//y velocity = -10, deltatime = 2(2 seconds per loop)
	//	-10*2 = -20, we missed on 20 units.
	//y velocity = -0.5, deltatime = 2(2 seconds per loop)
	//	-0.5*2 = -1

	//std::cout << "old position: " << getPosition() << "\n";
	//std::cout << "add velocity: " << (getVelocity()*deltaTime) << "\n";
	moveAdd(getVelocity()*deltaTime);

	//apply friction to velocity to prevent it moving infinitely
	//setVelocity(getVelocity() / (friction*deltaTime));
	//std::cout << "Added friction: " << friction*deltaTime << "\n";

	//std::cout << "velocity: " << getVelocity() << "\n";
	//std::cout << "new position: " << getPosition() << "\n";
	//al_rest(1);
}

void Entity::update(float deltaTime){
	//std::cout << "----------------------------\n";

	if (isInAir()){
		//add gravity :D
		//std::cout << "Deltatime: " << deltaTime << "\n";
		//std::cout << "Added gravity: " << gravity*deltaTime << "\n";
		
		//std::cout << "old velocity: " << getVelocity() << "\n";
		//9.81 p/s * time of a loop. 0.1 second per loop means 0.981 downward velocity
		setVelocity(getVelocity().addY( -(gravity*deltaTime) ) );
		//std::cout << "new velocity: " << getVelocity() << "\n";
	} else if (getVelocity().getY() != 0){
		//landed
		setVelocity(getVelocity().setY(0));
	}

	if (hasVelocity()) move(deltaTime);
}

void Entity::dump(){
	std::cout << getName().c_str() << ": position=[" << position << "] " << "rotation=[" << rotation << "]\n";
}