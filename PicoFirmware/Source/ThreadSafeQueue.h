#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <queue>
#include <pico/mutex.h>

template <typename T>
class ThreadSafeQueue {
private:
    std::queue<T> data_queue;
    mutable mutex_t mtx;

public:
    ThreadSafeQueue() {
        // Initialize the Pico SDK mutex
        mutex_init(&mtx);
    }

    // Non-copyable to prevent accidental copying of the mutex state
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    void push(T value) {
        // Use Pico SDK mutex guards
        mutex_enter_blocking(&mtx);
        data_queue.push(std::move(value));
        mutex_exit(&mtx);
    }
    
    // Non-blocking try_pop version (Safe for baremetal loops)
    // Returns true if a value was retrieved, false if queue empty.
    bool try_pop(T& value) {
        mutex_enter_blocking(&mtx);
        if (data_queue.empty()) {
            mutex_exit(&mtx);
            return false;
        }
        value = std::move(data_queue.front());
        data_queue.pop();
        mutex_exit(&mtx);
        return true;
    }

    // Optional: Check empty status (thread-safe)
    bool empty() const {
        mutex_enter_blocking(&mtx);
        bool result = data_queue.empty();
        mutex_exit(&mtx);
        return result;
    }
};

#endif // THREAD_SAFE_QUEUE_H