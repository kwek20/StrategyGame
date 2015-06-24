#include "Runnable.h"

#include "Game.h"

/*Runnable::Runnable(std::function<void(void)> task){
	this->task = task;
}*/

Runnable::Runnable(std::function<void(int count)> task){
	this->task1 = task;
}

Runnable::~Runnable(){
	
}

void Runnable::run(){
	if (task){
		task();
	} else {
		task1(count);
	}
	count++;
}

Task *Runnable::runTaskTimer(int delay, int period){
	return Game::get()->getScheduler()->runTaskTimer(this, delay, period);
}

Task *Runnable::runTaskTimerAsynch(int delay, int period){
	return Game::get()->getScheduler()->runTaskTimerAsynch(this, delay, period);
}
