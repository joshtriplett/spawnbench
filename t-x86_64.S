/* This test program gets the program start time as quickly as it can, then
 * writes it to file descriptor 4. It uses the raw clock_gettime syscall for
 * simplicity, to avoid having to parse the vdso. */

.text
.global _start
_start:
    mov $228, %ax /* clock_gettime */
    mov $4, %rdi /* CLOCK_MONOTONIC_RAW */
    mov %rsp, %rsi /* struct timespec * */
    syscall
    test %rax, %rax
    jnz 2f

    mov $1, %rax /* write */
    mov $4, %rdi /* fd */
    /* %rsi still contains timespec buffer */
    mov $16, %rdx /* sizeof(struct timespec) */
    syscall
    cmp $16, %rax
    jne 2f

    xor %rdi, %rdi /* exit code */
1:
    mov $60, %rax /* exit */
    syscall
2:
    mov $1, %rdi /* exit code */
    jmp 1b
