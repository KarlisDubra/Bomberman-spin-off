CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -g
LDFLAGS = -lglfw -lGL -lm
TARGET  = bomberman_test
SRCS    = main.c game.c map.c render.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all local clean

all: $(TARGET)

local: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

main.o: main.c game.h render.h
	$(CC) $(CFLAGS) -c -o $@ $<

render.o: render.c render.h game.h
	$(CC) $(CFLAGS) -c -o $@ $<

game.o: game.c game.h map.h
	$(CC) $(CFLAGS) -c -o $@ $<

map.o: map.c map.h game.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)