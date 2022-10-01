#define _GNU_SOURCE
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <sched.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const uint64_t ITERATIONS = 2000;

#define DO_PATH_SEARCH

#ifdef DO_PATH_SEARCH
#define exec_func execvpe
#define PATH_TO_T "t"
#else
#define exec_func execve
#define PATH_TO_T "./t"
#endif

pid_t do_spawn(const char *filename, char *const argv[], char *const envp[])
{
    pid_t pid;

#if defined(USE_fork) || defined(USE_vfork)
#if defined(USE_fork)
    pid = fork();
#else
    pid = vfork();
#endif
    if (pid == -1)
        err(1, "fork");
    if (pid == 0) {
        execvpe(filename, argv, envp);
        err(1, "execvpe");
    }
#elif defined(USE_posix_spawn)
    if (posix_spawnp(&pid, filename, NULL, NULL, argv, envp) != 0)
        err(1, "posix_spawnp");
#else
#error Unknown spawn method
#endif

    return pid;
}

const uint64_t NS_PER_SEC = 1000 * 1000 * 1000;

/* Subtract two timespecs and return 64-bit nanoseconds, erroring out if the
 * difference is more than a second. */
static inline uint64_t timespec_sub_small(struct timespec *end, struct timespec *start)
{
    uint64_t second_diff = end->tv_sec - start->tv_sec;
    if (second_diff > 1)
        errx(1, "spawning one process took much more than a second");
    uint64_t diff = (second_diff * NS_PER_SEC) + end->tv_nsec - start->tv_nsec;
    if (diff > NS_PER_SEC)
        errx(1, "spawning one process took more than a second");
    return diff;
}

const struct size {
    size_t mem;
    const char *desc;
    bool touch;
} SIZES[] = {
    { 1, "small", false }, // 1 rather than 0 to avoid NULL return
    { 1024 * 1024 * 1024, "+  1G", false },
    { 1024 * 1024 * 1024, "+  1G init", true },
    { (size_t)30 * 1024 * 1024 * 1024, "+ 30G", false },

};
const int N_SIZES = sizeof(SIZES) / sizeof(*SIZES);

/* We use assertions here on values that the assembly program hardcodes. */
int main(int argc, char *argv[], char *envp[])
{
    assert(sizeof(struct timespec) == 16);
    int pipes[2];
    if (pipe(pipes) < 0)
        err(1, "pipe");
    assert(pipes[0] == 3);
    assert(pipes[1] == 4);
    char *const t_argv[] = { "t", NULL };
    char *const t_envp[] = { NULL };

    for (int size_index = 0; size_index < N_SIZES; size_index++) {
        struct size size = SIZES[size_index];
        uint8_t *extra_mem = malloc(size.mem);
        if (!extra_mem) {
            err(1, "malloc");
        }
        // Touch the memory to make sure it's allocated
        if (size.touch)
            for (size_t i = 0; i < size.mem; i += 4096)
                extra_mem[i] = 1;

        uint64_t min_time = UINT64_MAX;
        uint64_t total_time = 0;

        for (int i = 0; i < ITERATIONS; i++) {
            struct timespec start, end;
            if (clock_gettime(CLOCK_MONOTONIC_RAW, &start) < 0)
                err(1, "pipe");
            do_spawn(PATH_TO_T, t_argv, t_envp);

            if (read(3, &end, 16) != 16)
                err(1, "read");

            uint64_t t = timespec_sub_small(&end, &start);
            if (t < min_time)
                min_time = t;
            total_time += t;
        }

        uint64_t avg_time = total_time / ITERATIONS;
        printf(
            "%14s %-10s: avg=%" PRIu64 "ns min=%" PRIu64 "ns\n",
            BENCH_NAME,
            size.desc,
            avg_time,
            min_time
        );

        free(extra_mem);
    }

    return 0;
}
