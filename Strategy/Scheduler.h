#ifndef __SCHEDULER_H_INCLUDED__
#define __SCHEDULER_H_INCLUDED__
#pragma once

#include <vector>
#include <atomic>

#include "Task.h"
#include "Runnable.h"

class Scheduler
{
public:
	Scheduler();
	~Scheduler();
private:
	int currentTicks;
	std::vector<Task*> tasks;
	std::atomic_int ids; 
	
	int nextId();
	Task *handle(Task *task, int delay);
public:
	// adds a tick to every currently active task
	void tick(int currentTicks);
	bool hasActiveTasks();
	void addTask(Task *task);
	Task *getTask(int id);

	bool removeTask(int id);
	bool removeTask(Task *task);
	void removeAll();

	Task * runTask(Runnable *runnable) { return runTaskDelayed(runnable, 0); }

	Task * runTaskDelayed(Runnable *runnable, int delay) { return runTaskTimer(runnable, delay, -1); }

	Task * runTaskTimer(Runnable *runnable, int period){ runTaskTimer(0, period); }
	Task * runTaskTimer(Runnable *runnable, int delay, int period);

	Task * runTaskDelayedAsynch(Runnable *runnable, int delay) { runTaskTimerAsynch(runnable, delay, -1); }

	Task * runTaskTimerAsynch(Runnable *runnable, int period){ runTaskTimerAsynch(runnable, 0, period); }
	Task * runTaskTimerAsynch(Runnable *runnable, int delay, int period);
};

#endif