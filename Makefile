CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = ipv4extractor
SRC = main.c

.PHONY: all run clean

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)