#include "Player.h"

void Player::moveAdd(Vec3<double> newPos){
	//newPos.setY(0);
	//position += newPos;

	Entity::moveAdd(newPos);
}

void Player::draw(){

}

void Player::update(float deltaTime){

}