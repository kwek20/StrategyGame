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

void Entity::load(Vec3<double> position, Vec3<double> rotation){
	// Set position, rotation 
	this->Object::load(position, rotation);

	// How fast we move (higher values mean we move and strafe faster)
	movementSpeedFactor = 100.0;

	//downward force! :D
	gravity = 9.81 * 6;

	//slight friction
	friction = 1.1;

	density = 1.293;
}

void Entity::draw(){
	GLUquadric *g = gluNewQuadric();
	gluQuadricNormals(g, GLU_SMOOTH);

	glTranslatef(getXPos(), getYPos(), getZPos());

	glColor3f(1, 0, 0);
	gluSphere(g, 1, 100, 10);

	gluDeleteQuadric(g);

	glColor4f(1, 1, 1, 1);
}

void Entity::draw2D(){
	int x, y;
	getPixelLocation(&x, &y);
	al_draw_text(al_create_builtin_font(), al_map_rgb(255, 0, 0), x, y, ALLEGRO_ALIGN_CENTER, getName().c_str());
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
	if (hasVelocity()){
		moveAdd(getVelocity()*deltaTime);
	}

	//apply friction to velocity to prevent it moving infinitely
	//setVelocity(getVelocity() / (friction*deltaTime));
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

	move(deltaTime);
}