#include <stdio.h>       // Стандартная библиотека ввода-вывода
#include <stdlib.h>      // Стандартная библиотека общих функций
#include <string.h>      // Библиотека для работы со строками
#include <dirent.h>      // Библиотека для работы с каталогами
#include <sys/stat.h>    // Библиотека для работы с информацией о файлах и каталогах
#include <unistd.h>      // Библиотека для доступа к POSIX API
#include <stdint.h>      // Библиотека для определения целочисленных типов с фиксированной шириной
#include <errno.h>       // Библиотека для обработки ошибок
#include <limits.h>      // Библиотека для определения пределов целочисленных типов
#include <pwd.h>         // Библиотека для получения информации о пользователе (Linux)
#include <libgen.h>      // Библиотека для функций dirname() и basename()

#define BUFFER_SIZE 1024        // Размер буфера для чтения и записи данных
#define FILE_ENTRY 0x01         // Константа, обозначающая файл в архиве
#define DIRECTORY_ENTRY 0x02    // Константа, обозначающая директорию в архиве
#define ARCHIVE_EXTENSION ".sa" // Расширение файлов архива

// Прототипы функций
const char *get_home_directory();                          // Получение домашней директории пользователя
int has_correct_extension(const char *filename, const char *extension); // Проверка расширения файла
void add_extension_if_missing(char *filename, const char *extension);   // Добавление расширения, если отсутствует
void print_usage(const char *program_name);                // Вывод инструкции по использованию программы
int create_directory(const char *path);                    // Создание директории, если она не существует
void rle_encode_file(FILE *in, FILE *out);                 // Кодирование файла с помощью RLE
void rle_decode_file(FILE *in, FILE *out);                 // Декодирование файла с помощью RLE
void write_entry(FILE *archive, const char *base_path, const char *relative_path); // Запись одной записи в архив
void pack_directory(FILE *archive, const char *base_path, const char *relative_path); // Рекурсивная упаковка директории
void pack(const char *input_path, char *archive_path);     // Архивирование файлов и директорий
void unpack(const char *archive_path, const char *output_folder);       // Разархивация архива
int main(int argc, char *argv[]);                          // Основная функция программы

// Функция для получения пути к домашней директории пользователя
const char *get_home_directory() {
    const char *home_dir = getenv("HOME");  // Получаем значение переменной окружения HOME
    if (home_dir == NULL) {  // Если переменная окружения HOME не установлена
        struct passwd *pw = getpwuid(getuid()); // Получаем информацию о текущем пользователе по его UID
        home_dir = pw->pw_dir;  // Получаем домашнюю директорию из структуры passwd
    }
    return home_dir;  // Возвращаем путь к домашней директории
}

// Функция для проверки, имеет ли файл указанное расширение
int has_correct_extension(const char *filename, const char *extension) {
    const char *dot = strrchr(filename, '.');  // Ищем последнюю точку в имени файла
    return dot && strcmp(dot, extension) == 0; // Сравниваем расширение файла с ожидаемым
}

// Функция для добавления расширения к имени файла, если оно отсутствует
void add_extension_if_missing(char *filename, const char *extension) {
    if (!has_correct_extension(filename, extension)) {
        strcat(filename, extension); // Добавляем расширение к имени файла
    }
}

// Функция для вывода инструкции по использованию программы
void print_usage(const char *program_name) {
    printf("Использование: %s <опция> <вход> [выход]\n", program_name);
    printf("Опции:\n");
    printf("  -pack <файл_или_папка> <архив>         Упаковать файл или папку в архив (.sa расширение требуется)\n");
    printf("  -unpack <архив> <папка>                Распаковать архив в папку\n");
    printf("  -pauto <файл_или_папка> [имя_архива]   Автоматически упаковать в указанный архив в папке Downloads (по умолчанию 'default_archive.sa')\n");
    printf("  -unauto <архив> [имя_папки]            Автоматически распаковать в указанную папку в папке Downloads (по умолчанию 'unpacked_folder')\n");
}

// Функция для создания директории, если она не существует
int create_directory(const char *path) {
    // Пытаемся создать директорию с правами доступа 0755
    if (mkdir(path, 0755) == 0 || (errno == EEXIST && access(path, F_OK) == 0)) {
        return 0; // Директория успешно создана или уже существует
    }
    perror("mkdir"); // Выводим сообщение об ошибке, если не удалось создать директорию
    return -1; // Возвращаем код ошибки
}

// Функция для кодирования файла с использованием алгоритма Run-Length Encoding (RLE)
void rle_encode_file(FILE *in, FILE *out) {
    int prev = fgetc(in); // Читаем первый символ из входного файла
    if (prev == EOF) return; // Если файл пустой, выходим из функции

    int count = 1; // Счетчик повторений символа
    int curr;

    // Читаем файл посимвольно и считаем количество повторений каждого символа
    while ((curr = fgetc(in)) != EOF) {
        if (curr == prev && count < 255) {
            count++; // Увеличиваем счетчик, если символ повторяется и счетчик меньше 255
        } else {
            fputc(count, out); // Записываем количество повторений
            fputc(prev, out);  // Записываем сам символ
            prev = curr;       // Переходим к следующему символу
            count = 1;         // Сбрасываем счетчик повторений
        }
    }
    // Записываем последние символы
    fputc(count, out);
    fputc(prev, out);
}

// Функция для декодирования файла, закодированного с помощью RLE
void rle_decode_file(FILE *in, FILE *out) {
    int count, byte;

    // Читаем данные из входного файла и восстанавливаем оригинальный файл
    while ((count = fgetc(in)) != EOF) {
        if ((byte = fgetc(in)) == EOF) break; // Если достигнут конец файла, выходим из цикла
        for (int i = 0; i < count; i++) {
            fputc(byte, out); // Записываем символ указанное количество раз
        }
    }
}

// Функция для записи одной записи (файла или директории) в архив
void write_entry(FILE *archive, const char *base_path, const char *relative_path) {
    char full_path[PATH_MAX];
    // Формируем полный путь к файлу или директории
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, relative_path);

    struct stat path_stat;
    // Получаем информацию о файле или директории
    if (stat(full_path, &path_stat) != 0) {
        perror("stat"); // Выводим сообщение об ошибке, если не удалось получить информацию
        return;
    }

    // Определяем тип записи: файл или директория
    uint8_t entry_type = S_ISDIR(path_stat.st_mode) ? DIRECTORY_ENTRY : FILE_ENTRY;
    uint16_t path_length = strlen(relative_path); // Длина относительного пути

    // Записываем тип записи и длину пути в архив
    fwrite(&entry_type, sizeof(uint8_t), 1, archive);
    fwrite(&path_length, sizeof(uint16_t), 1, archive);
    fwrite(relative_path, sizeof(char), path_length, archive); // Записываем относительный путь

    if (S_ISREG(path_stat.st_mode)) { // Если запись является файлом
        FILE *in = fopen(full_path, "rb"); // Открываем файл для чтения
        if (!in) {
            perror("fopen"); // Выводим сообщение об ошибке, если не удалось открыть файл
            return;
        }

        FILE *temp = tmpfile(); // Создаем временный файл для хранения сжатых данных
        if (!temp) {
            perror("tmpfile"); // Выводим сообщение об ошибке, если не удалось создать временный файл
            fclose(in);
            return;
        }

        rle_encode_file(in, temp); // Кодируем файл с помощью RLE и записываем в временный файл
        fclose(in); // Закрываем исходный файл

        uint64_t compressed_size = ftell(temp);   // Получаем размер сжатого файла
        fseek(temp, 0, SEEK_SET);                 // Возвращаемся в начало временного файла
        uint64_t original_size = path_stat.st_size; // Получаем размер исходного файла

        // Записываем размеры файлов в архив
        fwrite(&original_size, sizeof(uint64_t), 1, archive);   // Оригинальный размер файла
        fwrite(&compressed_size, sizeof(uint64_t), 1, archive); // Сжатый размер файла

        // Копируем сжатые данные из временного файла в архив
        char buffer[BUFFER_SIZE];
        size_t bytes;
        while ((bytes = fread(buffer, 1, BUFFER_SIZE, temp)) > 0) {
            fwrite(buffer, 1, bytes, archive);
        }
        fclose(temp); // Закрываем временный файл
    }
}

// Рекурсивная функция для упаковки директории и ее содержимого
void pack_directory(FILE *archive, const char *base_path, const char *relative_path) {
    char full_path[PATH_MAX];
    // Формируем полный путь к текущей директории или файлу
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, relative_path);

    struct stat path_stat;
    // Получаем информацию о текущей директории или файле
    if (stat(full_path, &path_stat) != 0) {
        perror("stat"); // Выводим сообщение об ошибке, если не удалось получить информацию
        return;
    }

    // Записываем текущую запись в архив
    write_entry(archive, base_path, relative_path);

    if (S_ISDIR(path_stat.st_mode)) { // Если текущая запись является директорией
        DIR *dir = opendir(full_path); // Открываем директорию для чтения
        if (!dir) {
            perror("opendir"); // Выводим сообщение об ошибке, если не удалось открыть директорию
            return;
        }

        struct dirent *entry;
        // Проходим по всем записям в директории
        while ((entry = readdir(dir)) != NULL) {
            // Пропускаем текущую и родительскую директории
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                char child_relative_path[PATH_MAX];
                // Формируем относительный путь к вложенной записи
                snprintf(child_relative_path, sizeof(child_relative_path), "%s/%s", relative_path, entry->d_name);
                // Рекурсивно пакуем вложенную директорию или файл
                pack_directory(archive, base_path, child_relative_path);
            }
        }
        closedir(dir); // Закрываем директорию
    }
}

// Функция для архивирования файлов и директорий
void pack(const char *input_path, char *archive_path) {
    // Добавляем расширение .sa к имени архива, если оно отсутствует
    add_extension_if_missing(archive_path, ARCHIVE_EXTENSION);

    FILE *archive = fopen(archive_path, "wb"); // Открываем архив для записи в бинарном режиме
    if (!archive) {
        perror("fopen"); // Выводим сообщение об ошибке, если не удалось открыть архив
        return;
    }

    // Получаем абсолютный путь к входному файлу или директории
    char *input_realpath = realpath(input_path, NULL);
    if (!input_realpath) {
        perror("realpath"); // Выводим сообщение об ошибке, если не удалось получить абсолютный путь
        fclose(archive);
        return;
    }

    // Создаем копии строк, так как функции dirname() и basename() могут модифицировать исходные строки
    char *input_dirname = strdup(input_realpath);
    char *input_basename = strdup(input_realpath);
    input_dirname = dirname(input_dirname);    // Получаем родительскую директорию
    input_basename = basename(input_basename); // Получаем имя входного файла или директории

    // Начинаем упаковку с родительской директории и относительного пути, содержащего имя входного файла/директории
    pack_directory(archive, input_dirname, input_basename);

    free(input_realpath); // Освобождаем выделенную память
    fclose(archive);      // Закрываем архив
}

// Функция для разархивации архива в указанную директорию
void unpack(const char *archive_path, const char *output_folder) {
    // Проверяем, имеет ли файл архива правильное расширение
    if (!has_correct_extension(archive_path, ARCHIVE_EXTENSION)) {
        fprintf(stderr, "Ошибка: архив должен иметь расширение %s\n", ARCHIVE_EXTENSION);
        return;
    }

    FILE *archive = fopen(archive_path, "rb"); // Открываем архив для чтения в бинарном режиме
    if (!archive) {
        perror("fopen"); // Выводим сообщение об ошибке, если не удалось открыть архив
        return;
    }

    // Создаем выходную директорию, если она не существует
    if (create_directory(output_folder) != 0) {
        fclose(archive);
        return;
    }

    // Читаем записи из архива и восстанавливаем файлы и директории
    while (1) {
        uint8_t entry_type;
        // Читаем тип записи (файл или директория)
        if (fread(&entry_type, sizeof(uint8_t), 1, archive) == 0) break; // Если достигнут конец архива, выходим из цикла

        uint16_t path_length;
        // Читаем длину относительного пути
        fread(&path_length, sizeof(uint16_t), 1, archive);

        char relative_path[PATH_MAX];
        // Читаем относительный путь
        fread(relative_path, sizeof(char), path_length, archive);
        relative_path[path_length] = '\0'; // Добавляем завершающий нулевой символ

        char full_path[PATH_MAX];
        // Формируем полный путь к файлу или директории в выходной директории
        snprintf(full_path, sizeof(full_path), "%s/%s", output_folder, relative_path);

        if (entry_type == DIRECTORY_ENTRY) { // Если запись является директорией
            create_directory(full_path); // Создаем директорию
        } else if (entry_type == FILE_ENTRY) { // Если запись является файлом
            uint64_t original_size, compressed_size;
            // Читаем оригинальный и сжатый размеры файла
            fread(&original_size, sizeof(uint64_t), 1, archive);
            fread(&compressed_size, sizeof(uint64_t), 1, archive);

            FILE *out = fopen(full_path, "wb"); // Открываем файл для записи
            if (!out) {
                perror("fopen"); // Выводим сообщение об ошибке, если не удалось открыть файл
                fclose(archive);
                return;
            }

            FILE *temp = tmpfile(); // Создаем временный файл для хранения сжатых данных
            if (!temp) {
                perror("tmpfile"); // Выводим сообщение об ошибке, если не удалось создать временный файл
                fclose(out);
                fclose(archive);
                return;
            }

            // Читаем сжатые данные из архива и записываем во временный файл
            char buffer[BUFFER_SIZE];
            size_t bytes_remaining = compressed_size;
            while (bytes_remaining > 0) {
                size_t bytes_to_read = bytes_remaining > BUFFER_SIZE ? BUFFER_SIZE : bytes_remaining;
                fread(buffer, 1, bytes_to_read, archive);
                fwrite(buffer, 1, bytes_to_read, temp);
                bytes_remaining -= bytes_to_read;
            }

            fseek(temp, 0, SEEK_SET); // Возвращаемся в начало временного файла
            rle_decode_file(temp, out); // Декодируем данные из временного файла и записываем в выходной файл

            fclose(out);  // Закрываем выходной файл
            fclose(temp); // Закрываем временный файл
        }
    }
    fclose(archive); // Закрываем архив
}

// Основная функция программы
int main(int argc, char *argv[]) {
    char default_archive_path[PATH_MAX];      // Буфер для хранения пути к архиву по умолчанию
    char default_output_folder[PATH_MAX];     // Буфер для хранения пути к выходной папке по умолчанию

    const char *home_dir = get_home_directory();  // Получаем домашнюю директорию пользователя

    if (argc < 3) { // Проверяем количество аргументов командной строки
        print_usage(argv[0]); // Выводим инструкцию по использованию программы
        return 1;
    }

    // Обработка различных опций командной строки
    if (strcmp(argv[1], "-pack") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        pack(argv[2], argv[3]); // Упаковываем указанный файл или директорию в архив
    } else if (strcmp(argv[1], "-unpack") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        unpack(argv[2], argv[3]); // Разархивируем архив в указанную директорию
    } else if (strcmp(argv[1], "-pauto") == 0) {
        if (argc < 3 || argc > 4) {
            print_usage(argv[0]);
            return 1;
        }
        // Используем указанное имя архива или имя по умолчанию "default_archive.sa"
        const char *archive_name = (argc == 4) ? argv[3] : "default_archive.sa";
        // Формируем полный путь к архиву в папке Downloads
        snprintf(default_archive_path, sizeof(default_archive_path), "%s/Downloads/%s", home_dir, archive_name);
        pack(argv[2], default_archive_path); // Автоматически упаковываем в папку Downloads
    } else if (strcmp(argv[1], "-unauto") == 0) {
        if (argc < 3 || argc > 4) {
            print_usage(argv[0]);
            return 1;
        }
        // Используем указанное имя папки или имя по умолчанию "unpacked_folder"
        const char *folder_name = (argc == 4) ? argv[3] : "unpacked_folder";
        // Формируем полный путь к выходной папке в папке Downloads
        snprintf(default_output_folder, sizeof(default_output_folder), "%s/Downloads/%s", home_dir, folder_name);
        unpack(argv[2], default_output_folder); // Автоматически разархивируем в папку Downloads
    } else {
        print_usage(argv[0]); // Выводим инструкцию по использованию, если опция не распознана
        return 1;
    }

    return 0; // Завершаем программу с кодом 0 (успешно)
}