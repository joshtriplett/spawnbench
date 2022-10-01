/* This test program gets the program start time as quickly as it can, then
 * writes it to file descriptor 4. It uses the raw clock_gettime syscall for
 * simplicity, to avoid having to parse the vdso. */

.text
.global _start
_start:
    mov x8, #113 /* clock_gettime */
    mov x0, #4 /* CLOCK_MONOTONIC_RAW */
    mov x1, sp /* struct timespec * */
    svc #0
    cbnz x0, 2f

    mov x8, #64 /* write */
    mov x0, #4 /* fd */
    /* x1 still contains timespec buffer */
    mov x2, #16 /* sizeof(struct timespec) */
    svc #0
    cmp x0, #16
    b.ne 2f

    mov x0, #0 /* exit code */
1:
    mov x8, #93 /* exit */
    svc #0
2:
    mov x0, #1 /* exit code */
    b 1b
