#ifndef __TASK_H_INCLUDED__
#define __TASK_H_INCLUDED__

#pragma once

class Runnable;

class Task
{
public:
	Task() : Task(nullptr, -1, -1) {}
	Task(Runnable *task) : Task(task, -1, -1) {}
	Task(Runnable *task, int id, int period);

	int getTaskId();
	bool isSync();
	void run();
	
	//-2 means cancel
	//-1 means no repeat
	// 0 should not happen
	// > 0 means delay between runs
	long getPeriod() { return period; }
	void setPeriod(long period); 

	long getNextRun(){ return nextRun; }
	void updateNextRun(long currentTicks) { nextRun = currentTicks + period; }
	void resetNextPeriod(){}

	//Task getNext(){ return next; }
	//void setNext(Task next) { this->next = next; }

	void cancel();
	bool cancel0();

	~Task();
private:
	int period;
	long nextRun;
	Runnable *task;
	int id;
};

#endif
