/*************************************************************************************[JobQueue.h]
MergeSat -- Copyright (c) 2009-2021,      Norbert Manthey

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

#ifndef MergeSat_JobQueue_h
#define MergeSat_JobQueue_h

#include <queue>

#include <pthread.h>
#include <semaphore.h>

namespace MERGESAT_NSPACE
{

bool isAdressContentZero(volatile void *address) { return address == 0; }

class JobQueue
{

    public:
    struct Job {
        void *(*function)(void *argument);
        void *argument;
        Job()
        {
            function = 0;
            argument = 0;
        };
    };

    static const int SLEEP = 0;
    static const int WORKING = 1;

    private:
    std::queue<Job> _jobqueue;
    sem_t _queueLocker;
    size_t _cpus;
    size_t _activecpus;
    size_t _currentWorkerNumber;
    sem_t *_sleepSem;
    volatile int *_threadState; // -1=toBeDestroyed 0=sleeping 1=working
    volatile int _workState;

    pthread_t *_threads;

    size_t getNextWorkerNumber()
    {
        size_t tmp;
        // lock queue
        sem_wait(&_queueLocker);
        tmp = _currentWorkerNumber;
        _currentWorkerNumber++;
        // unlock queue
        sem_post(&_queueLocker);
        return tmp;
    }

    // should be private!
    void wakeUpAll()
    {
        for (size_t i = 0; i < _cpus; i++) {
            sem_post(&(_sleepSem[i]));
        }
    }


    // Don't allow copying (error prone):
    JobQueue &operator=(JobQueue &other) = delete;
    JobQueue(const JobQueue &other) = delete;
    JobQueue(JobQueue &&other) = delete;

    public:
    /** create a job queue for a certain number of cpus/threads */
    JobQueue(size_t cpus = 0) :
        _jobqueue(),
        _cpus (0),
        _activecpus(0),
        _currentWorkerNumber (0),
        _sleepSem (nullptr),
        _threadState (nullptr),
        _workState (0),
        _threads(nullptr)
    {
        sem_init(&_queueLocker, 0, 1); // only one can take the semaphore
        if (cpus != 0) init(cpus);
    }

    /** init the queue for a number of threads
     * inits only, if the queue has not been initialized before with another number of cpus
     * @param cpus number of working threads
     */
    void init(size_t cpus)
    {
        if (_cpus != 0 || cpus == 0) return;
        _cpus = cpus;
        _activecpus = 0;
        _currentWorkerNumber = 0;

        _sleepSem = new sem_t[_cpus];
        _threadState = new int[_cpus];
        _threads = new pthread_t[_cpus];

        _workState = 0;

        // create threads

        for (size_t i = 0; i < _cpus; i++) {
            sem_init(&(_sleepSem[i]), 0, 0); // no space in semaphore
            _threadState[i] = 0;
            pthread_create(&(_threads[i]), 0, JobQueue::thread_func, (void *)this); // create thread
        }
    }

    ~JobQueue()
    {
        setState(-1);
        wakeUpAll();
    }

    int getThredState(size_t thread)
    {
        if (thread >= _cpus) return 0;
        return _threadState[thread];
    }

    void setState(int workState)
    {
        if (_workState == JobQueue::SLEEP && workState == JobQueue::WORKING) {
            _workState = workState;
            // set all the workStates before waking the threads up, to do not care about racing conditions!
            for (size_t i = 0; i < _cpus; i++) _threadState[i] = _workState;
            // wake Up all worker
            wakeUpAll();
        }
        _workState = workState;
    }

    Job getNextJob()
    {
        Job j;
        // lock queue
        sem_wait(&_queueLocker);
        if (_jobqueue.size() != 0) {
            j = _jobqueue.front();
            _jobqueue.pop();
        }
        // unlock queue
        sem_post(&_queueLocker);
        return j;
    }

    bool addJob(Job j)
    {
        // lock queue
        sem_wait(&_queueLocker);
        _jobqueue.push(j);
        // unlock queue
        sem_post(&_queueLocker);
        return true;
    }

    //	returns the size if multithread is needed, set locked to true
    size_t size(bool locked = false)
    {
        if (!locked) {
            return _jobqueue.size();
        } else {
            size_t qsize;
            // lock queue
            sem_wait(&_queueLocker);
            qsize = _jobqueue.size();
            // unlock queue
            sem_post(&_queueLocker);
            return qsize;
        }
    }

    bool allSleeping()
    {
        for (size_t i = 0; i < _cpus; ++i)
            if (_threadState[i] > 0) return false;
        return true;
    }

    void *run()
    {
        size_t myNumber = getNextWorkerNumber();
        sem_t *semaph = &(_sleepSem[myNumber]);

        // keep thread until workState is -1 (terminate)
        while (_workState != -1) {

            // check whether there is some work, do it
            JobQueue::Job job = getNextJob();
            if (job.function == 0) {
                // nothing to do -> sleep
                _threadState[myNumber] = JobQueue::SLEEP;
                sem_wait(semaph); // wait until waked up again
                _threadState[myNumber] = _workState;

            } else {
                // work on job!
                job.function(job.argument);
            }

            // check every round, whether to stop or not
            if (isAdressContentZero(&(_workState))) {
                // show last number ( 0 ) and sleep
                _threadState[myNumber] = JobQueue::SLEEP;
                sem_wait(semaph);
                // wake up and show new number
                _threadState[myNumber] = _workState;
            }
        }
        _threadState[myNumber] = _workState;
        return nullptr; // pointer!
    }

    static void *thread_func(void *d)
    {
        JobQueue *jqueue = (JobQueue *)d;
        return jqueue->run();
    }
};

}; // namespace MERGESAT_NSPACE

#endif
