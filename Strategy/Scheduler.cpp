#include "Scheduler.h"

#include <iostream>
#include <algorithm>

Scheduler::Scheduler(){

}


Scheduler::~Scheduler(){

}

// Checks currently active scheduled events for run
void Scheduler::tick(int currentTicks){
	this->currentTicks = currentTicks;

	auto tasks = this->tasks;
	for (Task *t : tasks){
		//-2 menas cancel
		if (t->getPeriod() == -2){
			t->cancel();
		}
		
		//weve reached the end!! :)
		if (t->getNextRun() == currentTicks){
			t->run();
			if (t->getPeriod() > 0){
				//Lets start over
				t->updateNextRun(currentTicks);
			} else if (t->getPeriod() == -1){
				//awwww we gotta go
				t->cancel();
			} else {
				//whuut
			}
		}
	}
}


bool Scheduler::hasActiveTasks(){
	return tasks.size() > 0;
}


void Scheduler::addTask(Task *task){
	tasks.push_back(task);
}

Task *Scheduler::getTask(int id){
	for (auto t : tasks){
		if (t->getTaskId() == id) return t;
	}
	return NULL;
}


bool Scheduler::removeTask(int id){
	return removeTask(getTask(id));
}

bool Scheduler::removeTask(Task *task){
	if (task == NULL) return false;

	task->cancel0();
	tasks.erase(std::remove(tasks.begin(), tasks.end(), task), tasks.end());

	return true;
}

void Scheduler::removeAll(){
	for (auto t : tasks){
		t->cancel();
	}
	tasks.clear();
}

Task *Scheduler::runTaskTimer(Runnable *runnable, int delay, int period){
	if (delay < 0) delay = 0;
	if (period == 0){
		period = 1;
	} else if (period < -1){
		period = -1;
	}
	return handle(new Task(runnable, nextId(), period), delay);
}

Task *Scheduler::runTaskTimerAsynch(Runnable *runnable, int delay, int period){
	return NULL;
}

Task *Scheduler::handle(Task *task, int delay){
	task->updateNextRun(currentTicks);
	addTask(task);
	return task;
}

int Scheduler::nextId(){
	int nextId = ids.fetch_add(1);
	return nextId;
}
