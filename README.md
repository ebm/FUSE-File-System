# Implementation
5 different queues to manage different aspects of thread management.<br>
queueList: Linked list which tail represents the next item in the queue, and head represents  the last item in the queue.<br>
waitList: A linked list containing threads that are currently waiting on another thread to finish (threads that call pthread_join).<br>
mutexList: A linked list containing all the created mutexes.<br>
blockList: A linked list containing threads that are blocked because they tried accessing mutexed code without owning the mutex.<br>
finishedList: A linked list containing finished threads with values to return.<br><br>

Timers<br>
noInterrupt: An integer variable that gets set to 1 when handleTimer() shouldn't switch the thread, and set to 0 when handleTimer() can switch the thread.<br>
handleTimerFailed: An integer variable that gets set to 1 when handleTimer() couldn't switch the thread the last time it was called. 0 otherwise.<br>
handleTimer(): A function that handles the timer signal. If noInterrupt is set, then handleTimer() sets handleTimerFailed to 1. If noInterrupt is not set, handleTimer() switches the context to the scheduleThread.<br>
resumeNormalContext() and keepContext(): Functions to manage the above variables to ensure thread-worker.c doesn't get interrupted in a critical activity. If handleTimer() does interrupt, the context will be switched at the end of the function instead of in the middle.<br><br>

Worker Creation:<br>
If this is the first time pthread_create was called, the function initializes scheduleThread, mainThread, timer, and all the necessary lists.<br>
In all cases of pthread_create, the function initializes a new thread and adds it to the queueList.<br>

Scheduler:<br>
While loop to ensure scheduler function does not stop till all threads stop running. Clears handleTimerFailed and noInterrupt in case thread might have set the noInterrupt variable. <br>
Checks if currentThread is not equal to the tail of the queueList. This might be the case if another function dequeued the currentThread to put in the waitList/blockList/finishedList. <br>
Under normal operations, currentThread will be dequeued from the tail of the queue and placed at the head. The elapsed variable in the TCB will be incremented.<br><br>
PSJF:<br>
- In PSJF, the function moveShortestJobToFront() gets called, which moves the job with the least elapsed time to the tail of the queue.<br>
MLFQ:<br>
- In MLFQ, if the total time quantum elapsed is a multiple of the REFRESH_QUANTUM (100 time quantums in this case), then the function moveAllJobsToPriority() is called. This sets each thread in the highest priority  queue.<br>
Otherwise, the function just gets the highest priority thread from the list to schedule.
Next, the scheduler will check if the tail has an element to put in the queue. If there is an element, the scheduler will calculate the response time if the thread has not been run before. The scheduler will also increment the tot_cntx_switches if the lastThread is different from the currentThread.
MLFQ:<br>
- In MLFQ, the scheduler will decrement the priority if the priority level is not already at the lowest level. The scheduler will also set the last priority change of the thread to the elapsed variable.<br>
At the end of the scheduler, the context is swapped to the new current thread. After another time quantum, the timeHandler will send us back to the next line and we will check if the queueList is empty.  If it is NULL, then we can assume that there are no more elements left to run and we will break out of the scheduler loop.<br><br>

Set Schedule Prio MLFQ:<br>
Function will search every queue by the threadID. If there are no threads with that ID, the function will return 1. Otherwise, the function will set the thread priority to the given priority, and the lastPriorityChange to elapsed.<br><br>

Yield:<br>
The function will check if the currentThread has used up its priority level. If it hasn't, the  function will add 1 back to the current priority (that was decremented in the scheduler) and swap the context back to the scheduler.<br><br>

Exit:<br>
The function will stop the timer and dequeue the currentThread from the queueList. If there are threads waiting for currentThread to finish, the function will add the those threads to the queueList. If value_ptr is not null and the thread(s) currently waiting on the currentThread have the value value_ptr_to_set set, then we know pthread_join has been called with a pointer. We can dereference value_ptr_to_set and set it to the value_ptr which will essentially set the return variable. The function will then set the turnaround time and free the stack space. If value_ptr is not null, then we set the value_ptr in the thread's tcb, and we added it to the list of finishedThreads. This is so that if this exit function was called before a thread_join, we can still keep track of the return value for the function. Otherwise, free the currentThread and set the context back to the scheduler.<br><br>

Join:<br>
This function will check if the thread id given has finished. <br>
- If it has finished, and the value_ptr is not null, then we can set the value_ptr to be the return value set by the worker_exit function. The function does this by checking the finishedList for the thread and getting its value_ptr. Then the function returns properly.<br>
If the thread has not finished, then the function will add the currentList to the waitList with a waitID equal to the thread id it is waiting for. If value_ptr is not null, the function will also set the value_ptr_to_set to the value_ptr so that when the thread finishes, the proper return value gets passed. The thread swaps the context back to the scheduler.<br><br>

Mutex Init:<br>
This function will initialize the mutex lists if they haven't already been initialized by pthread_create. The function sets the mutex variable at the pointer passed to be the next mutex ID. It sets the assignedTCB to -1 because the mutex is unowned, and it adds the mutex to the list of mutexes.<br><br>

Mutex Lock:<br>
The mutex first checks if the mutex is a valid mutex. If it's an invalid mutex, or the current owner of the mutex is trying to lock the mutex again, we will return 1.<br>
Otherwise, we have a while loop to continually check if the mutex is unowned. If it is unowned, then the while loop continues and we dequeue the currentThread from the scheduler and put it in the blockList. When the context switches back, the function will check if the mutex is unowned again. If it is unowned, then the function will set the assignedTCB to the currentThread.<br><br>

Mutex Unlock:<br>
The mutex will check if the mutex unlock failed because it was called twice, or called by a function that did not own the mutex, or if the mutex is invalid. It will return 1 if any of the above conditions are  true.<br>
If they aren't true, then we will unassign the TCB to be -1, and take all of the threads in the blockList who are trying to take control of the mutex and put them back into the queueList.<br><br>

Mutex Destroy:<br>
We will take any thread still in the blockList trying to get control over the mutex to the queueList.  We will then dequeue the mutex from the mutexList and return 0.
