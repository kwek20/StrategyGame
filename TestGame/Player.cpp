#include "Player.h"
#include <stdio.h>

void Player::moveAdd(Vec3<double> newPos){
	newPos.setY(0);
	position += newPos;
}