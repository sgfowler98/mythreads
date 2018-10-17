/* Sam Fowler
** CPSC3220
** mythreads.cc
** Threading library created for project 2
*/

#include <cassert>
#include <map>
#include <algorithm>
#include <signal.h>
#include <vector>
#include "mythreads.h"
#include <ucontext.h>
#include <cstdlib>
#include <iostream>

typedef void *(*thFuncPtr) (void *);
int interruptsAreDisabled;
void (*runFuncPtr) (thFuncPtr, void*);
int threadID, numThreadsAlive;

void runFunc(thFuncPtr, void*);

int getCurThread(void);
int grabNewThread(void);
static void interruptDisable(void);
static void interruptEnable(void);

//struct container for a thread
struct thread_t {
	int tid;
	ucontext_t *context;
	bool status; //true if running, false otherwise
	bool alive;
	bool locked;
	void *ret;

//initialization list for emplace efficiency
	thread_t(int tid, ucontext_t *context, bool status, bool alive, bool locked, void* ret)
		 : tid(tid), context(context), status(status), alive(alive), locked(locked), ret(ret) {}
};

std::vector<thread_t*> threads;
std::map<int,thread_t*> locks, waiting;

//starts the library by initializing interruptsAreDisabled, threadID, the runFunc pointer, and numThreadsAlive
void threadInit() {
	interruptsAreDisabled = 1;
	threadID = 0;
	runFuncPtr = &runFunc;
	ucontext_t *maincontext = new ucontext_t();
	getcontext(maincontext);
	threads.emplace_back(new thread_t(threadID, maincontext, true, true, false, operator new(sizeof(void*))));
	numThreadsAlive = 1;
	interruptEnable();
}

//creates a thread
int threadCreate(thFuncPtr funcPtr, void *argPtr) {
	interruptDisable();
	threadID++;

	//fill most of the soon-to-be-used context
	ucontext_t *newcontext = new ucontext_t();
	getcontext(newcontext);

	//fill thread's stack
	newcontext->uc_stack.ss_sp = operator new(STACK_SIZE);
	newcontext->uc_stack.ss_size = STACK_SIZE;
	newcontext->uc_stack.ss_flags = 0;

	//deactivate current thread
	thread_t *tptr = threads[getCurThread()];
	tptr->status = false;

	//create context, then add a new active thread to the thread manager
	makecontext(newcontext, reinterpret_cast<void(*)(void)>(runFuncPtr), 2, funcPtr, argPtr);
	threads.emplace_back(new thread_t(threadID, newcontext, true, true, false, operator new(sizeof(void*))));
	numThreadsAlive++;

	//begin executing the new thread
	swapcontext(tptr->context, threads[threadID]->context);
	interruptEnable();

	return threadID;
}

//yields execution to next available thread
void threadYield() {
	interruptDisable();

	//can't yield if there are no other threads
	if (numThreadsAlive <= 1) {
		return;
	}

	//swap thread's running status then swap the contexts
	thread_t *curptr = threads[getCurThread()];
	thread_t *newptr = threads[grabNewThread()];
	curptr->status = false;
	newptr->status = true;
	swapcontext(curptr->context, newptr->context);
	interruptEnable();
}

//joins a thread until exit
void threadJoin(int thread_id, void **result) {
	if (thread_id >= threads.size()) { //invalid thread id
		return;
	} else if (threads[thread_id]->alive == false) { //dead thread, give return value
		*result = threads[thread_id]->ret;
		return;
	} else { //valid join
		thread_t *curptr, *newptr;
		do {
			//continue to swap to desired thread until exit
			interruptDisable();
			curptr = threads[getCurThread()];
			newptr = threads[thread_id];
			curptr->status = false;
			newptr->status = true;
			swapcontext(curptr->context, newptr->context);
			interruptEnable();
		} while(newptr->alive == true);
		*result = (newptr->ret);
	}
}

//kill a thread
void threadExit(void *result) {
	interruptDisable();
	thread_t *curptr = threads[getCurThread()];
	thread_t *newptr = threads[grabNewThread()];

	//if main thread
	if (curptr->tid == 0) {
		interruptEnable();
		exit(0);
	}

	//tag thread as dead and save result
	numThreadsAlive--;
	curptr->status = false;
	curptr->alive = false;
	curptr->ret = result;
	newptr->status = true;

	//begin executing new thread
	swapcontext(curptr->context, newptr->context);
	interruptEnable();
}

//locks a thread
void threadLock(int lockNum) {
	//attempts to acquire a lock, executes another thread if lock is taken until it can acquire the lock
	while (locks.find(lockNum) != locks.end()) {
		interruptDisable();
		thread_t *curptr = threads[getCurThread()];
		thread_t *newptr = threads[grabNewThread()];
		curptr->status = false;
		newptr->status = true;
		swapcontext(curptr->context, newptr->context);
		interruptEnable();
	}
	//tag thread as locked and add to locked list
	interruptDisable();
	thread_t *curptr = threads[getCurThread()];
	curptr->locked = true;
	locks.insert(std::pair<int,thread_t*>(lockNum, curptr));
	interruptEnable();
}

//unlocks a thread
void threadUnlock(int lockNum) {
	interruptDisable();
	if (locks.find(lockNum) == locks.end()) { //lockNum was not locked
		interruptEnable();
		return;
	} else { //tag thread as unlocked and remove entry from locked list
		locks.find(lockNum)->second->locked = false;
		locks.erase(lockNum);
		interruptEnable();
	}
}

//waits on a thread
void threadWait(int lockNum, int conditionNum) {
	if (locks.find(lockNum) == locks.end()) { //if lock is unlocked
		std::cerr << "\nthreadWait called with an unlocked lock, aborting\n";
		exit(0);
	}

	//insert to waiting list and unlock
	threadUnlock(lockNum);
	thread_t *curptr = threads[getCurThread()];
	waiting.insert(std::pair<int,thread_t*>(conditionNum, curptr));

	//block until threadSignal is called with the necessary parameters
	while (waiting.find(conditionNum) != waiting.end()) {
		interruptDisable();
		thread_t *curptr = threads[getCurThread()];
		thread_t *newptr = threads[grabNewThread()];
		curptr->status = false;
		newptr->status = true;
		swapcontext(curptr->context, newptr->context);
		interruptEnable();
	}
	threadLock(lockNum);
}

//sends a signal
void threadSignal(int lockNum, int conditionNum) {
	if (waiting.find(conditionNum) != waiting.end()) { //if valid call, remove from waiting list,
		waiting.erase(conditionNum);			//freeing the thread in wait
	}
}

//runs a function and saves return
void runFunc(thFuncPtr thptr, void *args) {
	thread_t *curptr = threads[getCurThread()];
	interruptEnable();
	curptr->ret = (*(thptr))(args);
	threadExit(curptr->ret);
}

//gets current thread index
int getCurThread() {
	for (auto thread : threads) {
		if (thread->status == true) {
			return thread->tid;
		}
	}
	return -1;
}

//gets next available thread index
int grabNewThread() {
	int index;
	thread_t *curptr = threads[getCurThread()];
	for (int i = 1; i <= threads.size(); i++) {
		index = (curptr->tid + i) % threads.size();
		if (threads[index]->status == false && threads[index]->alive) {
			return threads[index]->tid;
		}
	}
	return -1;
}

static void interruptDisable() {
	assert(!interruptsAreDisabled);
	interruptsAreDisabled = 1;
}

static void interruptEnable() {
	assert(interruptsAreDisabled);
	interruptsAreDisabled = 0;
}
