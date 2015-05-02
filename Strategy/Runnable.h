#ifndef __RUNNABLE_H_INCLUDED__
#define __RUNNABLE_H_INCLUDED__

#pragma once

#include "Task.h"
#include <functional>

class Runnable
{
public:
	Runnable(std::function<void(void)> task);
	~Runnable();

	void run();

	Task * runTask() { return runTaskDelayed(0); }

	Task * runTaskDelayed(int delay) { return runTaskTimer(delay, -1); }

	Task * runTaskTimer(int period){ runTaskTimer(0, period); }
	Task * runTaskTimer(int delay, int period);

	Task * runTaskDelayedAsynch(int delay) { runTaskTimerAsynch(delay, -1); }

	Task * runTaskTimerAsynch(int period){ runTaskTimerAsynch(0, period); }
	Task * runTaskTimerAsynch(int delay, int period);

	void cancelTask();
	int getTaskId();
	int getCount() { return count; }
private:
	void checkState();

	int count;
	std::function<void(void)> task;
};

#endif