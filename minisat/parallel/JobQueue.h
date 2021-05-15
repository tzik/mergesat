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

#include <cassert>
#include <condition_variable>

namespace MERGESAT_NSPACE
{

bool isAdressContentZero(volatile void *address) { return address == 0; }

/* object that blocks threads until a predefined number of threads reached a given point */
class Barrier
{
    public:
    Barrier(size_t nb_threads = 0)
      : m_mutex(), m_condition(), m_nb_threads(nb_threads), m_capacity(nb_threads), m_count_down(true)
    {
    }

    Barrier(const Barrier &barrier) = delete;
    Barrier(Barrier &&barrier) = delete;
    ~Barrier() noexcept
    {
        assert((0u == m_nb_threads || m_nb_threads == m_capacity) && "do not destruct with sleeping threads");
    }
    Barrier &operator=(const Barrier &barrier) = delete;
    Barrier &operator=(Barrier &&barrier) = delete;

    /* wait in this method until the predefined number of threads have reached this function call */
    void wait()
    {
        std::unique_lock<std::mutex> lock{ m_mutex };

        if (m_count_down) /* we are currently decrementing */
        {
            assert(0u != m_nb_threads);
            /* counting down */
            if (--m_nb_threads == 0) {
                m_count_down = !m_count_down;
                m_condition.notify_all();
            } else {
                /* block while counting down */
                m_condition.wait(lock, [this] { return m_count_down == false; });
            }
        }

        else /* we are currently incrementing */
        {
            assert(0u != m_capacity);
            /* counting up */
            if (++m_nb_threads == m_capacity) {
                m_count_down = !m_count_down;
                m_condition.notify_all();
            } else {
                /* block while counting up */
                m_condition.wait(lock, [this] { return m_count_down == true; });
            }
        }
    }

    /* allow a greater number of threads to be blocked, return success */
    bool grow(size_t new_capacity)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        /* cannot remove threads from the barrier */
        if (new_capacity < m_capacity) return false;

        /* if we are currently decrementing, increase number of threads that need to enter */
        if (m_count_down) {
            m_nb_threads += new_capacity - m_capacity;
        }

        /* otherwise, number of threads that still need to enter will increase automatically with the next line */
        m_capacity = new_capacity;

        assert(m_capacity >= m_nb_threads && "cannot have more threads than capacity");
        return true;
    }

    /* allow to check how many threads still need to enter before all are released */
    size_t remaining(bool locked = false)
    {
        assert(m_capacity >= m_nb_threads && "cannot have more threads than capacity");
        if (locked) {
            std::unique_lock<std::mutex> lock(m_mutex);
            return (m_count_down ? m_nb_threads : m_capacity - m_nb_threads);
        }
        return (m_count_down ? m_nb_threads : m_capacity - m_nb_threads);
    }

    /* indicate whether currently no thread is blocking in this barrier */
    bool empty(bool locked = false) { return remaining(locked) == m_capacity; }

    /* signal number of threads to be blocked */
    size_t capacity(bool locked = false)
    {
        if (locked) {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_capacity;
        }
        return m_capacity;
    }

    private:
    std::mutex m_mutex;                  // lock variable
    std::condition_variable m_condition; // variable to block on, with sleeping
    size_t m_nb_threads; // number of threads that need to hit the block before continuing (oth that already hit the block, when counting up)
    size_t m_capacity; // number of expected threads when resetting the barrier
    bool m_count_down; // store state, whether we currently increment or decrement
};


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
    static const int TERMINATE = -1;

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
        wait_terminate();
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

    /* wait for all threads to terminate */
    void wait_terminate()
    {
        setState(TERMINATE);
        wakeUpAll();

        for (size_t i = 0; i < _cpus; i++) {
            pthread_join(_threads[i], NULL);
        }
    }

    void *run()
    {
        size_t myNumber = getNextWorkerNumber();
        sem_t *semaph = &(_sleepSem[myNumber]);

        // keep thread until workState is -1 (terminate)
        while (_workState != TERMINATE) {

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
