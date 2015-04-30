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

	long getPeriod() { return period; }
	void setPeriod(long period);

	long getNextRun(){ return nextRun; }

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
