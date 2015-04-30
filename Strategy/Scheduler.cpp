#include "Scheduler.h"

Scheduler::Scheduler(){

}


Scheduler::~Scheduler(){

}


// adds a tick to every currently active task
void Scheduler::tick(){
	for (auto t : tasks){
		switch (t->getPeriod()){
			case -2: 
				removeTask(t);
				break;
			default: break;
		}

		t->setPeriod(t->getPeriod() - 1);
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

	task->cancel();
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
	
	return task;
}

int Scheduler::nextId(){
	return ids.fetch_add(1);
}
