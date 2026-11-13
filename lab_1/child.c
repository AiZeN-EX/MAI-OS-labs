#include <unistd.h>
#include <stdlib.h>
#include <float.h>

int strfromf(char *str, size_t size, const char *format, float value);

int main(void) {
    char c;
    char word[256];
    int len = 0;
    int count = 0;
    int eof = 0;
    float result = 0;

    while (!eof) {
        ssize_t n = read(0, &c, 1);
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
                    number != number || number > FLT_MAX || number < -FLT_MAX) {
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
                    if (result != result || result > FLT_MAX || result < -FLT_MAX) {
                        write(2, "Некорректный результат\n", sizeof("Некорректный результат\n") - 1);
                        return 1;
                    }
                }
                count++;
                len = 0;
            }
            if (c == '\n') {
                if (count == 1) {
                    write(2, "Недостаточно чисел\n", sizeof("Недостаточно чисел\n") - 1);
                    return 1;
                }
                if (count > 1) {
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
                        return 1;
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
                    if (write(1, output, size) != size) {
                        write(2, "Ошибка записи\n", sizeof("Ошибка записи\n") - 1);
                        return 1;
                    }
                }
                count = 0;
                result = 0;
            }
        } else {
            if (len >= 255) {
                write(2, "Слишком длинное число\n", sizeof("Слишком длинное число\n") - 1);
                return 1;
            }
            word[len++] = c;
        }
    }
    return 0;
}
