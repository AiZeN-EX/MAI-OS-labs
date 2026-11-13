#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(void) {
    char filename[256];
    char buffer[256];

    if (write(1, "Имя файла: ", sizeof("Имя файла: ") - 1) < 0) {
        return 1;
    }

    ssize_t n = read(0, filename, sizeof(filename) - 1);
    if (n <= 0) {
        write(2, "Ошибка ввода\n", sizeof("Ошибка ввода\n") - 1);
        return 1;
    }

    int len = 0;
    while (len < n && filename[len] != '\n') {
        len++;
    }
    if (len == 0 || len == sizeof(filename) - 1) {
        write(2, "Неверное имя файла\n", sizeof("Неверное имя файла\n") - 1);
        return 1;
    }
    filename[len] = '\0';

    int file = open(filename, O_RDONLY);
    if (file < 0) {
        write(2, "Ошибка открытия файла\n", sizeof("Ошибка открытия файла\n") - 1);
        return 1;
    }

    int fd[2];
    if (pipe(fd) < 0) {
        write(2, "Ошибка pipe\n", sizeof("Ошибка pipe\n") - 1);
        close(file);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        write(2, "Ошибка fork\n", sizeof("Ошибка fork\n") - 1);
        close(file);
        close(fd[0]);
        close(fd[1]);
        return 1;
    }

    if (pid == 0) {
        close(fd[0]);
        if (dup2(file, 0) < 0 || dup2(fd[1], 1) < 0) {
            write(2, "Ошибка dup2\n", sizeof("Ошибка dup2\n") - 1);
            _exit(1);
        }
        close(file);
        close(fd[1]);

        char *args[] = {"./child", 0};
        char *env[] = {0};
        execve("./child", args, env);
        write(2, "Ошибка запуска child\n", sizeof("Ошибка запуска child\n") - 1);
        _exit(1);
    }

    close(file);
    close(fd[1]);

    int error = 0;
    while (1) {
        n = read(fd[0], buffer, sizeof(buffer));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            write(2, "Ошибка чтения\n", sizeof("Ошибка чтения\n") - 1);
            error = 1;
            break;
        }
        if (write(1, buffer, n) != n) {
            write(2, "Ошибка записи\n", sizeof("Ошибка записи\n") - 1);
            error = 1;
            break;
        }
    }
    close(fd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    if (error || !WIFEXITED(status)) {
        return 1;
    }
    return WEXITSTATUS(status);
}
