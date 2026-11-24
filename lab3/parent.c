#define _POSIX_C_SOURCE 200809L
#define __STDC_WANT_IEC_60559_BFP_EXT__ 1
#include <stdlib.h>
#include "shared.h"
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int open_input(void) {
    char name[256];
    fputs("Имя файла: ", stderr);
    if (!fgets(name, sizeof(name), stdin)) {
        fputs("Ошибка ввода имени файла\n", stderr);
        return -1;
    }
    size_t len = 0;
    while (name[len] != '\0' && name[len] != '\r' && name[len] != '\n') {
        len++;
    }
    if (len == sizeof(name) - 1) {
        fputs("Слишком длинное имя файла\n", stderr);
        return -1;
    }
    name[len] = '\0';
    int fd = open(name, O_RDONLY);
    if (fd < 0) perror("Ошибка открытия файла");
    return fd;
}

pid_t wait_child(pid_t pid, int *status, int options) {
    pid_t result;
    do {
        result = waitpid(pid, status, options);
    } while (result < 0 && errno == EINTR);
    return result;
}

int print_result(float result) {
    char output[128];
    int precision = 6;
    double x = result < 0 ? -result : result;
    while (x >= 10 && precision > 0) {
        x /= 10;
        precision--;
    }
    while (x > 0 && x < 1) {
        x *= 10;
        precision++;
    }
    char format[] = "%.00f";
    format[2] = '0' + precision / 10;
    format[3] = '0' + precision % 10;
    int size = strfromf(output, sizeof(output), format, result == 0 ? 0.0f : result);
    if (size < 0 || size >= (int)sizeof(output) - 1) {
        write(2, "Ошибка преобразования\n", sizeof("Ошибка преобразования\n") - 1);
        return -1;
    }
    if (precision > 0) {
        while (size > 0 && output[size - 1] == '0') {
            size--;
        }
        if (size > 0 && output[size - 1] == '.') {
            size--;
        }
    }
    output[size++] = '\n';
    int written = 0;
    while (written < size) {
        ssize_t n = write(STDOUT_FILENO, output + written, size - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            perror("Ошибка вывода результата");
            return -1;
        }
        written += (int)n;
    }
    return 0;
}

int wait_message(pid_t pid, sem_t *full, int *status, int *reaped) {
    while (!*reaped) {
        struct timespec deadline;
        if (clock_gettime(CLOCK_REALTIME, &deadline) < 0) {
            perror("clock_gettime");
            return 0;
        }
        deadline.tv_sec++;
        if (sem_timedwait(full, &deadline) == 0) return 1;
        if (errno == EINTR) continue;
        if (errno != ETIMEDOUT) {
            perror("sem_timedwait");
            return 0;
        }
        pid_t result = wait_child(pid, status, WNOHANG);
        if (result < 0) {
            perror("waitpid");
            return 0;
        }
        if (result == pid) {
            *reaped = 1;
            if (sem_trywait(full) == 0) return 1;
            if (errno != EAGAIN) perror("sem_trywait");
        }
    }
    return 0;
}

int receive(pid_t pid, Message *message, sem_t *empty, sem_t *full) {
    int status = 0;
    int reaped = 0;
    int done = 0;
    while (wait_message(pid, full, &status, &reaped)) {
        if (message->done) {
            done = 1;
            break;
        }
        if (print_result(message->result) < 0) break;
        if (sem_post(empty) < 0) {
            perror("sem_post");
            break;
        }
    }
    if (!reaped) {
        if (!done && kill(pid, SIGKILL) < 0 && errno != ESRCH) perror("kill");
        if (wait_child(pid, &status, 0) < 0) {
            perror("waitpid");
            return 1;
        }
    }
    if (!WIFEXITED(status)) {
        fputs("Дочерний процесс аварийно завершён\n", stderr);
        return 1;
    }
    if (WEXITSTATUS(status) != 0) return WEXITSTATUS(status);
    return done ? 0 : 1;
}

void check_cleanup(int result, const char *operation, int *status) {
    if (result < 0) {
        perror(operation);
        *status = 1;
    }
}

int main(void) {
    int file = open_input();
    if (file < 0) return 1;

    char shm_name[64], empty_name[64], full_name[64];
    long id = (long)getpid();
    snprintf(shm_name, sizeof(shm_name), "/mai9_shm_%ld", id);
    snprintf(empty_name, sizeof(empty_name), "/mai9_empty_%ld", id);
    snprintf(full_name, sizeof(full_name), "/mai9_full_%ld", id);

    int status = 1;
    int flags = O_CREAT | O_EXCL | O_RDWR;
    int shm = shm_open(shm_name, flags, 0600);
    Message *message = MAP_FAILED;
    sem_t *empty = SEM_FAILED, *full = SEM_FAILED;
    if (shm < 0) {
        perror("shm_open");
        goto cleanup;
    }
    if (ftruncate(shm, sizeof(Message)) < 0) {
        perror("ftruncate");
        goto cleanup;
    }
    message = mmap(NULL, sizeof(Message), PROT_READ, MAP_SHARED, shm, 0);
    if (message == MAP_FAILED) {
        perror("mmap");
        goto cleanup;
    }
    empty = sem_open(empty_name, O_CREAT | O_EXCL, 0600, 1);
    if (empty == SEM_FAILED) {
        perror("sem_open empty");
        goto cleanup;
    }
    full = sem_open(full_name, O_CREAT | O_EXCL, 0600, 0);
    if (full == SEM_FAILED) {
        perror("sem_open full");
        goto cleanup;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        goto cleanup;
    }
    if (pid == 0) {
        if (dup2(file, STDIN_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }
        if (file != STDIN_FILENO) close(file);
        char *args[] = {"./child", shm_name, empty_name, full_name, NULL};
        char *env[] = {NULL};
        execve("./child", args, env);
        perror("Ошибка запуска child");
        _exit(1);
    }
    status = receive(pid, message, empty, full);

cleanup:
    if (full != SEM_FAILED) {
        check_cleanup(sem_close(full), "sem_close full", &status);
        check_cleanup(sem_unlink(full_name), "sem_unlink full", &status);
    }
    if (empty != SEM_FAILED) {
        check_cleanup(sem_close(empty), "sem_close empty", &status);
        check_cleanup(sem_unlink(empty_name), "sem_unlink empty", &status);
    }
    if (message != MAP_FAILED)
        check_cleanup(munmap(message, sizeof(Message)), "munmap", &status);
    if (shm >= 0) {
        check_cleanup(close(shm), "close shm", &status);
        check_cleanup(shm_unlink(shm_name), "shm_unlink", &status);
    }
    check_cleanup(close(file), "close file", &status);
    return status;
}
