#include <stdio.h>       // Библиотека для работы с вводом и выводом
#include <stdlib.h>      // Библиотека для работы с памятью, преобразованиями и системными вызовами
#include <string.h>      // Библиотека для работы со строками
#include <dirent.h>      // Библиотека для работы с директориями
#include <sys/stat.h>    // Библиотека для получения информации о файлах и директориях
#include <unistd.h>      // Библиотека для доступа к POSIX API
#include <stdint.h>      // Библиотека для определения целочисленных типов с фиксированной шириной
#include <errno.h>       // Библиотека для обработки ошибок
#include <limits.h>      // Библиотека для определения размеров числовых типов
#include <pwd.h>     // Для работы с пользовательской информацией (для Linux)

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
    const char *dot = strrchr(filename, '.');
    return dot && strcmp(dot, extension) == 0;
}

// Функция для автоматического добавления расширения .sa, если оно отсутствует
void add_extension_if_missing(char *filename, const char *extension) {
    if (!has_correct_extension(filename, extension)) {
        strcat(filename, extension);
    }
}

// Функция, которая выводит на экран инструкцию по использованию программы
void print_usage(const char *program_name) {
    printf("Usage: %s <option> <input> [output]\n", program_name);
    printf("Options:\n");
    printf("  -pack <file_or_folder> <archive>       Pack file or folder into archive (.sa extension required)\n");
    printf("  -unpack <archive> <folder>             Unpack archive into folder\n");
    printf("  -pauto <file_or_folder> [archive_name] Automatically pack into specified archive in the Downloads folder (defaults to 'default_archive.sa')\n");
    printf("  -unauto <archive> [folder_name]        Automatically unpack into specified folder in the Downloads folder (defaults to 'unpacked_folder')\n");
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
    int prev = fgetc(in);
    if (prev == EOF) return;

    int count = 1;
    int curr;

    while ((curr = fgetc(in)) != EOF) {
        if (curr == prev && count < 255) {
            count++;
        } else {
            fputc(count, out);
            fputc(prev, out);
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
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, relative_path);

    struct stat path_stat;
    if (stat(full_path, &path_stat) != 0) {
        perror("stat");
        return;
    }

    uint8_t entry_type = S_ISDIR(path_stat.st_mode) ? DIRECTORY_ENTRY : FILE_ENTRY;
    uint16_t path_length = strlen(relative_path);

    fwrite(&entry_type, sizeof(uint8_t), 1, archive);
    fwrite(&path_length, sizeof(uint16_t), 1, archive);
    fwrite(relative_path, sizeof(char), path_length, archive);

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

        rle_encode_file(in, temp);
        fclose(in);

        uint64_t compressed_size = ftell(temp);
        fseek(temp, 0, SEEK_SET);
        uint64_t original_size = path_stat.st_size;

        fwrite(&original_size, sizeof(uint64_t), 1, archive);
        fwrite(&compressed_size, sizeof(uint64_t), 1, archive);

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
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, relative_path);

    struct stat path_stat;
    if (stat(full_path, &path_stat) != 0) {
        perror("stat");
        return;
    }

    write_entry(archive, base_path, relative_path);

    if (S_ISDIR(path_stat.st_mode)) {
        DIR *dir = opendir(full_path);
        if (!dir) {
            perror("opendir");
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                char child_relative_path[PATH_MAX];
                snprintf(child_relative_path, sizeof(child_relative_path), "%s/%s", relative_path, entry->d_name);
                pack_directory(archive, base_path, child_relative_path);
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

    char base_path[PATH_MAX];
    realpath(input_path, base_path);
    pack_directory(archive, base_path, ".");

    fclose(archive);
}


// Разархивация архивов в директории
void unpack(const char *archive_path, const char *output_folder) {
    if (!has_correct_extension(archive_path, ARCHIVE_EXTENSION)) {
        fprintf(stderr, "Error: Archive file must have the extension %s\n", ARCHIVE_EXTENSION);
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
            create_directory(full_path);
        } else if (entry_type == FILE_ENTRY) {
            uint64_t original_size, compressed_size;
            fread(&original_size, sizeof(uint64_t), 1, archive);
            fread(&compressed_size, sizeof(uint64_t), 1, archive);

            FILE *out = fopen(full_path, "wb");
            FILE *temp = tmpfile();

            char buffer[BUFFER_SIZE];
            size_t bytes_remaining = compressed_size;
            while (bytes_remaining > 0) {
                size_t bytes_to_read = bytes_remaining > BUFFER_SIZE ? BUFFER_SIZE : bytes_remaining;
                fread(buffer, 1, bytes_to_read, archive);
                fwrite(buffer, 1, bytes_to_read, temp);
                bytes_remaining -= bytes_to_read;
            }

            fseek(temp, 0, SEEK_SET);
            rle_decode_file(temp, out);

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
        pack(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-unpack") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        unpack(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-pauto") == 0) {
        if (argc < 3 || argc > 4) {
            print_usage(argv[0]);
            return 1;
        }
        // Use user-specified name or default to "default_archive.sa"
        const char *archive_name = (argc == 4) ? argv[3] : "default_archive.sa";
        snprintf(default_archive_path, sizeof(default_archive_path), "%s/Downloads/%s", home_dir, archive_name);
        pack(argv[2], default_archive_path);
    } else if (strcmp(argv[1], "-unauto") == 0) {
        if (argc < 3 || argc > 4) {
            print_usage(argv[0]);
            return 1;
        }
        // Use user-specified name or default to "unpacked_folder"
        const char *folder_name = (argc == 4) ? argv[3] : "unpacked_folder";
        snprintf(default_output_folder, sizeof(default_output_folder), "%s/Downloads/%s", home_dir, folder_name);
        unpack(argv[2], default_output_folder);
    } else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}