# Название итогового исполняемого файла
TARGET = archiver

# Компилятор
CC = gcc

# Флаги компилятора
CFLAGS = -Wall -pthread -lm

# Исходные файлы
SRC = ./src/archiver.c

# Правило по умолчанию
all: $(TARGET)

# Правило сборки программы
$(TARGET): $(SRC) $(STB_FILES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# Очистка скомпилированных файлов
clean:
	rm -f $(TARGET)

# Очистка скомпилированных файлов и временных файлов редакторов
distclean: clean
	rm -f *~

# Правило для повторной сборки программы
rebuild: distclean all
