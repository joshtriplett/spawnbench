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
#define posix_spawn_func posix_spawnp
#define PATH_TO_T "t"
#else
#define exec_func execve
#define posix_spawn_func posix_spawn
#define PATH_TO_T "./t"
#endif

#ifdef USE_io_uring_spawn
#include "liburing.h"

struct io_uring ring;

void do_setup(void)
{
    int ret = io_uring_queue_init(ITERATIONS * 8, &ring, 0);
    if (ret)
        errx(1, "io_uring_queue_init");
    ret = io_uring_register_ring_fd(&ring);
    if (ret != 1)
        errx(1, "io_uring_register_ring_fd");
}
#else
void do_setup(void)
{
}
#endif

#ifdef USE_clone_vm
static uint8_t clone_vm_stack[4096];
static const char *clone_vm_filename;
static char *const *clone_vm_argv;
static char *const *clone_vm_envp;

static int clone_vm_target(void *_unused)
{
    exec_func(clone_vm_filename, clone_vm_argv, clone_vm_envp);
    err(1, "exec");
}
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
        exec_func(filename, argv, envp);
        err(1, "exec");
    }
#elif defined(USE_posix_spawn)
    if (posix_spawn_func(&pid, filename, NULL, NULL, argv, envp) != 0)
        err(1, "posix_spawn");
#elif defined(USE_clone_vm)
    clone_vm_filename = filename;
    clone_vm_argv = argv;
    clone_vm_envp = envp;
    pid = clone(clone_vm_target, clone_vm_stack+sizeof(clone_vm_stack)-1, CLONE_VM | SIGCHLD, NULL);
    if (pid == -1)
        err(1, "clone");
#elif defined(USE_io_uring_spawn)
    struct io_uring_sqe *sqe;
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_clone(sqe);
    io_uring_sqe_set_flags(sqe, IOSQE_IO_LINK);
#ifdef DO_PATH_SEARCH
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_exec(sqe, "/usr/local/bin/t", argv, envp);
    io_uring_sqe_set_flags(sqe, IOSQE_IO_HARDLINK);
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_exec(sqe, "/usr/local/sbin/t", argv, envp);
    io_uring_sqe_set_flags(sqe, IOSQE_IO_HARDLINK);
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_exec(sqe, "/usr/bin/t", argv, envp);
    io_uring_sqe_set_flags(sqe, IOSQE_IO_HARDLINK);
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_exec(sqe, "/usr/sbin/t", argv, envp);
    io_uring_sqe_set_flags(sqe, IOSQE_IO_HARDLINK);
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_exec(sqe, "/bin/t", argv, envp);
    io_uring_sqe_set_flags(sqe, IOSQE_IO_HARDLINK);
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_exec(sqe, "/sbin/t", argv, envp);
    io_uring_sqe_set_flags(sqe, IOSQE_IO_HARDLINK);
#endif
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_exec(sqe, "./t", argv, envp);
    io_uring_submit(&ring);
    return -1;
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

    do_setup();

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
