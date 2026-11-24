#define _POSIX_C_SOURCE 200809L
#include "shared.h"
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <math.h>

int send_message(Message *message, sem_t *empty, sem_t *full,
                        float result, int done) {
    while (sem_wait(empty) < 0) {
        if (errno != EINTR) {
            perror("sem_wait");
            return -1;
        }
    }
    message->result = result;
    message->done = done;
    if (sem_post(full) < 0) {
        perror("sem_post");
        return -1;
    }
    return 0;
}

int calculate(Message *message, sem_t *empty, sem_t *full) {
    char c;
    char word[256];
    int len = 0;
    int count = 0;
    int eof = 0;
    float result = 0;

    while (!eof) {
        ssize_t n = read(0, &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            write(2, "Ошибка чтения\n", sizeof("Ошибка чтения\n") - 1);
            return 1;
        }
        if (n == 0) {
            c = '\n';
            eof = 1;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (len > 0) {
                word[len] = '\0';
                char *end;
                float number = strtof(word, &end);
                if (end == word || *end != '\0' ||
                    !isfinite(number)) {
                    write(2, "Некорректное число\n", sizeof("Некорректное число\n") - 1);
                    return 1;
                }
                if (count == 0) {
                    result = number;
                } else {
                    if (number == 0) {
                        write(2, "Деление на ноль\n", sizeof("Деление на ноль\n") - 1);
                        return 2;
                    }
                    result /= number;
                    if (!isfinite(result)) {
                        write(2, "Некорректный результат\n", sizeof("Некорректный результат\n") - 1);
                        return 1;
                    }
                }
                if (count < 2) count++;
                len = 0;
            }
            if (c == '\n') {
                if (count == 1) {
                    write(2, "Недостаточно чисел\n", sizeof("Недостаточно чисел\n") - 1);
                    return 1;
                }
                if (count > 1) {
                    if (send_message(message, empty, full, result, 0) < 0) {
                        return 1;
                    }
                }
                count = 0;
                result = 0;
            }
        } else {
            if (c == '\0') {
                write(2, "Некорректное число\n", sizeof("Некорректное число\n") - 1);
                return 1;
            }
            if (len >= 255) {
                write(2, "Слишком длинное число\n", sizeof("Слишком длинное число\n") - 1);
                return 1;
            }
            word[len++] = c;
        }
    }
    return send_message(message, empty, full, 0, 1) < 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fputs("Запускайте программу через ./parent\n", stderr);
        return 1;
    }
    int fd = shm_open(argv[1], O_RDWR, 0);
    if (fd < 0) {
        perror("child: shm_open");
        return 1;
    }
    Message *message = mmap(NULL, sizeof(Message), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (message == MAP_FAILED) {
        perror("child: mmap");
        return 1;
    }
    if (close(fd) < 0) {
        perror("child: close");
        return 1;
    }
    sem_t *empty = sem_open(argv[2], 0);
    if (empty == SEM_FAILED) {
        perror("child: sem_open empty");
        return 1;
    }
    sem_t *full = sem_open(argv[3], 0);
    if (full == SEM_FAILED) {
        perror("child: sem_open full");
        return 1;
    }
    int status = calculate(message, empty, full);
    if (sem_close(empty) < 0) {
        perror("sem_close empty");
        status = 1;
    }
    if (sem_close(full) < 0) {
        perror("sem_close full");
        status = 1;
    }
    if (munmap(message, sizeof(Message)) < 0) {
        perror("munmap");
        status = 1;
    }
    return status;
}
