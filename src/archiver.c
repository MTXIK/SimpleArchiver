#include <stdio.h>       // Библиотека для работы с вводом и выводом
#include <stdlib.h>      // Библиотека для работы с памятью, преобразованиями и системными вызовами
#include <string.h>      // Библиотека для работы со строками
#include <dirent.h>      // Библиотека для работы с директориями
#include <sys/stat.h>    // Библиотека для получения информации о файлах и директориях
#include <unistd.h>      // Библиотека для доступа к POSIX API
#include <stdint.h>      // Библиотека для определения целочисленных типов с фиксированной шириной
#include <errno.h>       // Библиотека для обработки ошибок
#include <limits.h>      // Библиотека для определения размеров числовых типов
#include <pwd.h>         // Для работы с пользовательской информацией (для Linux)
#include <libgen.h>      // Для функций dirname() и basename()

#define BUFFER_SIZE 1024        // Определяем размер буфера для чтения и записи данных
#define FILE_ENTRY 0x01         // Константа, указывающая на запись типа "файл" в архиве
#define DIRECTORY_ENTRY 0x02    // Константа, указывающая на запись типа "директория" в архиве
#define ARCHIVE_EXTENSION ".sa" // Расширение для архивных файлов

// Прототипы функций
// Функция для получения пути к домашней директории пользователя
const char *get_home_directory();
// Функция для проверки, что файл имеет правильное расширение
int has_correct_extension(const char *filename, const char *extension);
// Функция для автоматического добавления расширения .sa, если оно отсутствует
void add_extension_if_missing(char *filename, const char *extension);
// Функция, которая выводит на экран инструкцию по использованию программы
void print_usage(const char *program_name);
// Функция для создания директории, если она не существует
int create_directory(const char *path);
// Функция для кодирования файла с использованием алгоритма RLE
void rle_encode_file(FILE *in, FILE *out);
// Функция для декодирования файла с использованием алгоритма RLE
void rle_decode_file(FILE *in, FILE *out);
// Функция записи одного файла или директории в архив
void write_entry(FILE *archive, const char *base_path, const char *relative_path);
// Рекурсивное сжатие директории
void pack_directory(FILE *archive, const char *base_path, const char *relative_path);
// Архивирование файлов и директорий
void pack(const char *input_path, char *archive_path);
// Разархивация архивов в директории
void unpack(const char *archive_path, const char *output_folder);
// Основная функция программы
int main(int argc, char *argv[]);

// Функция для получения пути к домашней директории пользователя
const char *get_home_directory() {
    const char *home_dir = getenv("HOME");  // Получаем домашнюю директорию из переменной окружения
    if (home_dir == NULL) {  // Если переменная окружения не задана (бывает в редких случаях)
        struct passwd *pw = getpwuid(getuid());
        home_dir = pw->pw_dir;  // Получаем домашнюю директорию с помощью структуры passwd
    }
    return home_dir;
}

// Функция для проверки, что файл имеет правильное расширение
int has_correct_extension(const char *filename, const char *extension) {
    const char *dot = strrchr(filename, '.');  // Ищем последнюю точку в имени файла
    return dot && strcmp(dot, extension) == 0; // Сравниваем расширение
}

// Функция для автоматического добавления расширения .sa, если оно отсутствует
void add_extension_if_missing(char *filename, const char *extension) {
    if (!has_correct_extension(filename, extension)) {
        strcat(filename, extension); // Добавляем расширение к имени файла
    }
}

// Функция, которая выводит на экран инструкцию по использованию программы
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
    if (mkdir(path, 0755) == 0 || (errno == EEXIST && access(path, F_OK) == 0)) {
        return 0; // Директория успешно создана или уже существует
    }
    perror("mkdir");
    return -1; // Возвращаем код ошибки
}

// Функция для кодирования файла с использованием алгоритма RLE
void rle_encode_file(FILE *in, FILE *out) {
    int prev = fgetc(in); // Читаем первый байт
    if (prev == EOF) return;

    int count = 1;
    int curr;

    // Проходим по файлу и считаем повторяющиеся байты
    while ((curr = fgetc(in)) != EOF) {
        if (curr == prev && count < 255) {
            count++;
        } else {
            fputc(count, out); // Записываем количество повторений
            fputc(prev, out);  // Записываем сам байт
            prev = curr;
            count = 1;
        }
    }
    fputc(count, out);
    fputc(prev, out);
}

// Функция для декодирования файла с использованием алгоритма RLE
void rle_decode_file(FILE *in, FILE *out) {
    int count, byte;

    // Читаем количество повторений и байт, затем записываем байт указанное количество раз
    while ((count = fgetc(in)) != EOF) {
        if ((byte = fgetc(in)) == EOF) break;
        for (int i = 0; i < count; i++) {
            fputc(byte, out);
        }
    }
}

// Функция записи одного файла или директории в архив
void write_entry(FILE *archive, const char *base_path, const char *relative_path) {
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, relative_path); // Формируем полный путь

    struct stat path_stat;
    if (stat(full_path, &path_stat) != 0) {
        perror("stat");
        return;
    }

    uint8_t entry_type = S_ISDIR(path_stat.st_mode) ? DIRECTORY_ENTRY : FILE_ENTRY; // Определяем тип записи
    uint16_t path_length = strlen(relative_path); // Длина относительного пути

    fwrite(&entry_type, sizeof(uint8_t), 1, archive);          // Записываем тип записи
    fwrite(&path_length, sizeof(uint16_t), 1, archive);        // Записываем длину пути
    fwrite(relative_path, sizeof(char), path_length, archive); // Записываем относительный путь

    if (S_ISREG(path_stat.st_mode)) {
        FILE *in = fopen(full_path, "rb");
        if (!in) {
            perror("fopen");
            return;
        }

        FILE *temp = tmpfile();
        if (!temp) {
            perror("tmpfile");
            fclose(in);
            return;
        }

        rle_encode_file(in, temp); // Кодируем файл с помощью RLE
        fclose(in);

        uint64_t compressed_size = ftell(temp);   // Получаем размер сжатого файла
        fseek(temp, 0, SEEK_SET);
        uint64_t original_size = path_stat.st_size; // Размер исходного файла

        fwrite(&original_size, sizeof(uint64_t), 1, archive);   // Записываем оригинальный размер
        fwrite(&compressed_size, sizeof(uint64_t), 1, archive); // Записываем сжатый размер

        // Копируем сжатые данные в архив
        char buffer[BUFFER_SIZE];
        size_t bytes;
        while ((bytes = fread(buffer, 1, BUFFER_SIZE, temp)) > 0) {
            fwrite(buffer, 1, bytes, archive);
        }
        fclose(temp);
    }
}

// Рекурсивное сжатие директории
void pack_directory(FILE *archive, const char *base_path, const char *relative_path) {
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, relative_path); // Формируем полный путь

    struct stat path_stat;
    if (stat(full_path, &path_stat) != 0) {
        perror("stat");
        return;
    }

    write_entry(archive, base_path, relative_path); // Записываем текущую запись в архив

    if (S_ISDIR(path_stat.st_mode)) {
        DIR *dir = opendir(full_path);
        if (!dir) {
            perror("opendir");
            return;
        }

        struct dirent *entry;
        // Проходим по всем элементам в директории
        while ((entry = readdir(dir)) != NULL) {
            // Пропускаем текущую и родительскую директории
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                char child_relative_path[PATH_MAX];
                snprintf(child_relative_path, sizeof(child_relative_path), "%s/%s", relative_path, entry->d_name);
                pack_directory(archive, base_path, child_relative_path); // Рекурсивно пакуем поддиректории и файлы
            }
        }
        closedir(dir);
    }
}

// Архивирование файлов и директорий
void pack(const char *input_path, char *archive_path) {
    // Добавляем расширение .sa, если оно отсутствует
    add_extension_if_missing(archive_path, ARCHIVE_EXTENSION);

    FILE *archive = fopen(archive_path, "wb");
    if (!archive) {
        perror("fopen");
        return;
    }

    // Получаем абсолютный путь к входному файлу или директории
    char *input_realpath = realpath(input_path, NULL);
    if (!input_realpath) {
        perror("realpath");
        fclose(archive);
        return;
    }

    // Получаем родительскую директорию и имя входного файла/папки
    char *input_dirname = strdup(input_realpath);
    char *input_basename = strdup(input_realpath);
    input_dirname = dirname(input_dirname);    // Родительская директория
    input_basename = basename(input_basename); // Имя файла или папки

    // Начинаем упаковку с родительской директории, используя имя входного файла/папки как относительный путь
    pack_directory(archive, input_dirname, input_basename);

    free(input_realpath);
    fclose(archive);
}

// Разархивация архивов в директории
void unpack(const char *archive_path, const char *output_folder) {
    if (!has_correct_extension(archive_path, ARCHIVE_EXTENSION)) {
        fprintf(stderr, "Ошибка: архив должен иметь расширение %s\n", ARCHIVE_EXTENSION);
        return;
    }

    FILE *archive = fopen(archive_path, "rb");
    if (!archive) {
        perror("fopen");
        return;
    }

    if (create_directory(output_folder) != 0) {
        fclose(archive);
        return;
    }

    // Читаем записи из архива и восстанавливаем структуру
    while (1) {
        uint8_t entry_type;
        if (fread(&entry_type, sizeof(uint8_t), 1, archive) == 0) break;

        uint16_t path_length;
        fread(&path_length, sizeof(uint16_t), 1, archive);

        char relative_path[PATH_MAX];
        fread(relative_path, sizeof(char), path_length, archive);
        relative_path[path_length] = '\0';

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", output_folder, relative_path);

        if (entry_type == DIRECTORY_ENTRY) {
            create_directory(full_path); // Создаем директорию
        } else if (entry_type == FILE_ENTRY) {
            uint64_t original_size, compressed_size;
            fread(&original_size, sizeof(uint64_t), 1, archive);
            fread(&compressed_size, sizeof(uint64_t), 1, archive);

            FILE *out = fopen(full_path, "wb");
            if (!out) {
                perror("fopen");
                fclose(archive);
                return;
            }

            FILE *temp = tmpfile();
            if (!temp) {
                perror("tmpfile");
                fclose(out);
                fclose(archive);
                return;
            }

            // Читаем сжатые данные из архива
            char buffer[BUFFER_SIZE];
            size_t bytes_remaining = compressed_size;
            while (bytes_remaining > 0) {
                size_t bytes_to_read = bytes_remaining > BUFFER_SIZE ? BUFFER_SIZE : bytes_remaining;
                fread(buffer, 1, bytes_to_read, archive);
                fwrite(buffer, 1, bytes_to_read, temp);
                bytes_remaining -= bytes_to_read;
            }

            fseek(temp, 0, SEEK_SET);
            rle_decode_file(temp, out); // Декодируем данные и записываем в файл

            fclose(out);
            fclose(temp);
        }
    }
    fclose(archive);
}

// Основная функция программы
int main(int argc, char *argv[]) {
    char default_archive_path[PATH_MAX];
    char default_output_folder[PATH_MAX];

    const char *home_dir = get_home_directory();  // Определяем домашнюю директорию пользователя

    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-pack") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        pack(argv[2], argv[3]); // Упаковываем указанный файл или папку в архив
    } else if (strcmp(argv[1], "-unpack") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        unpack(argv[2], argv[3]); // Распаковываем архив в указанную папку
    } else if (strcmp(argv[1], "-pauto") == 0) {
        if (argc < 3 || argc > 4) {
            print_usage(argv[0]);
            return 1;
        }
        // Используем указанное имя архива или по умолчанию "default_archive.sa"
        const char *archive_name = (argc == 4) ? argv[3] : "default_archive.sa";
        snprintf(default_archive_path, sizeof(default_archive_path), "%s/Downloads/%s", home_dir, archive_name);
        pack(argv[2], default_archive_path); // Автоматически упаковываем в папку Downloads
    } else if (strcmp(argv[1], "-unauto") == 0) {
        if (argc < 3 || argc > 4) {
            print_usage(argv[0]);
            return 1;
        }
        // Используем указанное имя папки или по умолчанию "unpacked_folder"
        const char *folder_name = (argc == 4) ? argv[3] : "unpacked_folder";
        snprintf(default_output_folder, sizeof(default_output_folder), "%s/Downloads/%s", home_dir, folder_name);
        unpack(argv[2], default_output_folder); // Автоматически распаковываем в папку Downloads
    } else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}