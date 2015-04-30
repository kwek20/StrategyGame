#include "Task.h"

#include "Runnable.h"

Task::Task(Runnable *task, int id, int period){
	this->task = task;
	this->id = id;
	this->period = period;
}

Task::~Task(){

}

void Task::setPeriod(long period){ 
	this->period = period; 

	if (period == 0){
		run();
	}
};

int Task::getTaskId(){
	return id;
}

bool Task::isSync(){
	return true;
}

void Task::run(){
	task->run();
	cancel();
}

void Task::cancel(){
	cancel0();
}

bool Task::cancel0(){
	setPeriod(-2);
	return true;
}
