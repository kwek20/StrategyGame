#include "Task.h"

#include "Runnable.h"
#include "Game.h"

Task::Task(Runnable *task, int id, int period){
	this->task = task;
	this->id = id;
	this->period = period;
}

Task::~Task(){

}

void Task::setPeriod(long period){ 
	this->period = period; 
}

int Task::getTaskId(){
	return id;
}

bool Task::isSync(){
	return true;
}

void Task::run(){
	task->run();
}

void Task::cancel(){
	Game::get()->getScheduler()->removeTask(this);
}

bool Task::cancel0(){
	setPeriod(-2);
	return true;
}
