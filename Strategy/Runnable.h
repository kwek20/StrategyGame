#pragma once
class Runnable
{
public:
	Runnable();
	~Runnable();

	void runTask();
	void runDelayed(int delay);
	void runTimer(int period);

	void runDelayedAsynch(int delay);
	void runTimerAsynch(int period);

	void cancelTask();
	int getTaskId();

};

