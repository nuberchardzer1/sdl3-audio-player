# sdl2-audio-player
SDL2-based audio player in C with basic controls and signal visualization.

## run 
```bash
cc main.c $(pkg-config --cflags --libs sdl3 sdl3-image sdl3-ttf) -lm && ./a.out input.wav
```