CC := gcc
LD := gcc

CFLAGS := -O2 -Wall -Wextra -std=gnu99
LIBS := -lkeystone

SRC := assemble_file.c
OBJ := $(SRC:.c=.o)

TARGET := assemble_file

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(LD) $(LIBS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f *.o $(TARGET)

run: $(TARGET)
	./$(TARGET)
