#include <iostream>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

static const long PERIOD_NS = 100 * 1000 * 1000; // 100 ms

timespec add_timespec(const timespec& t, long ns) {
    timespec result = t;
    result.tv_nsec += ns;
    while (result.tv_nsec >= 1000000000) {
        result.tv_nsec -= 1000000000;
        result.tv_sec += 1;
    }
    return result;
}

int main() {

    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        std::cerr << "Memory locking failed!" << std::endl;
        return 1;
    }

    timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    long buffer[50] = {};

    for (int i = 0; i < 50; i++) {
        

        timespec actual;
        clock_gettime(CLOCK_MONOTONIC, &actual);

        long jitter_ns =
            (actual.tv_sec - next.tv_sec) * 1000000000L +
            (actual.tv_nsec - next.tv_nsec);

        buffer[i] = jitter_ns;

        // simulate workload (RT "task")
        // std::cout << "Tick " << i
        //           << " at " << next.tv_sec << "."
        //           << next.tv_nsec << std::endl;

        // compute next activation time (fixed period)
        next = add_timespec(next, PERIOD_NS);

        // sleep until exact next time (RT behavior)
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
    }

    // Print jitter values
    std::cout << "Jitter values (ns):" << std::endl;
    for (int i = 0; i < 50; i++) {
        std::cout << "Tick " << i << ": " << buffer[i] << " ns" << std::endl;
    }   

    return 0;
}