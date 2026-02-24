CC=gcc
CFLAGS=-lSDL3 -lSDL3_ttf -lSDL3_image -lm

SRC=src/main.c
OUT=build/audio_player

all: build $(OUT)

build:
	mkdir -p build

$(OUT): $(SRC)
	$(CC) $(SRC) $(CFLAGS) -o $(OUT)