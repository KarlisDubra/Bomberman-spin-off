CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -g
LDFLAGS = -lglfw -lGL -lm
ifeq ($(OS),Windows_NT)
LDFLAGS = -lglfw3 -lopengl32 -lgdi32 -lws2_32 -lm
endif
TARGET  = bomberman_client
SRCS    = main.c game.c map.c render.c net_client.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all local clean

all: $(TARGET)

local: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

main.o: main.c game.h render.h net.h
	$(CC) $(CFLAGS) -c -o $@ $<

net_client.o: net_client.c net.h game.h
	$(CC) $(CFLAGS) -c -o $@ $<

render.o: render.c render.h game.h
	$(CC) $(CFLAGS) -c -o $@ $<

game.o: game.c game.h map.h
	$(CC) $(CFLAGS) -c -o $@ $<

map.o: map.c map.h game.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
