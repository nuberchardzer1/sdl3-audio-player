#include <math.h>
#include <stdio.h>

#include <SDL2/SDL.h>
#include <SDL_image.h>

#include "debug.h"

/* Constants */
#define RUNNING 1
#define STOPPED 0
#define WAV_HEADER_SIZE 44
#define MAX_HEADER_SIZE 44

/* Global variables*/
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Surface *screen = NULL;

static SDL_Rect r_menu;
static int menu_padding_x = 100;
static int menu_padding_y = 20;
static int menu_height = 50; 

static SDL_Rect r_timeline;
static int timeline_padding_x = 50;
static int timeline_padding_y = 20;
static int timeline_height = 50; 

static SDL_Rect r_play;
static int play_btn_height = 50; 
static int play_btn_weight = 50; 

static SDL_Texture *pause_icon = NULL;
static SDL_Texture *play_icon = NULL;

static int WINDOW_WIDTH = 800;
static int WINDOW_HEIGHT = 600;

static char *progname;
static char *audio_file_path;
static FILE *audio_file;

static SDL_AudioDeviceID device_id;

struct audio_buffer{
    Uint32 len;
    Uint8 *buf;
    SDL_AudioSpec spec; 
};

typedef struct rgba{
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a; 
} rgba;

static rgba *bkg_pcolor = NULL; 
static rgba *graphic_pcolor = NULL; 

/** * Test whether an x/y coordinate is within a given rect. * 
 * 
 * @param r The rect 
 * @param x X coordinate 
 * @param y Y coordinate 
 * @return true if a hit, else false */ 
static int hit_test(const SDL_Rect *const r, unsigned x, unsigned y){ 
    /* note the use of unsigned math: no need to check for negative */ 
    return (x - (unsigned)r->x < (unsigned)r->w) && (y - (unsigned)r->y < (unsigned)r->h); } 


float *calculate_peaks(int16_t *samples, int windows, int window_samples);
float *calculate_rms(int16_t *samples, int windows, int window_samples); 

#define SDL_SetRenderDrawColorRGBA(renderer, colorp) \
    SDL_SetRenderDrawColor((renderer), (colorp)->r, (colorp)->g, (colorp)->b, (colorp)->a)

void setup_screen_layout(struct audio_buffer *audio){
    bkg_pcolor = malloc(sizeof *bkg_pcolor);
    bkg_pcolor->r = 209;
    bkg_pcolor->g = 229;
    bkg_pcolor->b = 244;
    bkg_pcolor->a = 255;

    graphic_pcolor = malloc(sizeof *graphic_pcolor);
    graphic_pcolor->r = 22;
    graphic_pcolor->g = 30;
    graphic_pcolor->b = 26;
    graphic_pcolor->a = 255;

    // init static
    pause_icon = IMG_LoadTexture(renderer, "../assets/img/pause_icon.png");
    if (!pause_icon){
        printf("Error: IMG_LoadTexture failed (pause_icon): %s\n", SDL_GetError());
    }
    SDL_SetTextureScaleMode(pause_icon, SDL_ScaleModeBest);
    
    play_icon = IMG_LoadTexture(renderer, "../assets/img/play_icon.png");
    if (!play_icon){
        printf("Error: IMG_LoadTexture failed (play_icon): %s\n", SDL_GetError());
    }   
    SDL_SetTextureScaleMode(play_icon, SDL_ScaleModeBest);

    const int padding = 150;

    SDL_SetRenderDrawColorRGBA(renderer, bkg_pcolor);
    SDL_RenderClear(renderer);

    const int bar_width = 2;
    const int gap = 1;
    
    int graphic_lines = WINDOW_WIDTH / (bar_width + gap);
    int offset = bar_width + gap;

    int total_samples = audio->len / sizeof(int16_t);
    int window_samples = total_samples / graphic_lines;

    int windows = graphic_lines;

    int16_t *samples = (int16_t *)audio->buf;

    float *rms = calculate_rms(samples, windows, window_samples);

    SDL_SetRenderDrawColorRGBA(renderer, graphic_pcolor);

    int xpos = 0; 
    const int y1 = padding;
    const int y2 = WINDOW_HEIGHT - padding;
    const int max_line_length = y2 - y1;

    for (int i = 0; i < windows; i++){
        float coeff = rms[i];
        // printf("peak %d --> %0.2f\n", i, coeff);
        int line_padding = (int)(max_line_length * (1 - coeff) / 3);
        SDL_RenderDrawLine(renderer, xpos, y1 + line_padding, xpos, y2 - line_padding);
        xpos += offset;
    }

    free(rms);

    //menu
    r_menu.x = menu_padding_x;
    r_menu.y = WINDOW_HEIGHT - menu_padding_y - menu_height;
    r_menu.h = menu_height;
    r_menu.w = WINDOW_WIDTH - menu_padding_x * 2;

    r_timeline.x = r_menu.x + timeline_padding_x;
    r_timeline.w = r_menu.w - timeline_padding_x;
    r_timeline.y = r_menu.y + timeline_padding_y;
    r_timeline.h = r_menu.h - timeline_padding_y;

    r_play.w = play_btn_weight;
    r_play.h = play_btn_height;
    r_play.x = r_menu.x + r_menu.w / 2 - play_btn_weight / 2;
    r_play.y = r_menu.y;


    SDL_RenderCopy(renderer, pause_icon, NULL, &r_play); 

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    // SDL_RenderDrawRect(renderer, &r_menu);
    // SDL_RenderDrawRect(renderer, &r_timeline);

    SDL_RenderPresent(renderer);
}

int setup_sdl(struct audio_buffer *audio){
    #if defined(__linux__)
        const char *session = getenv("XDG_SESSION_TYPE");
        if (session && strcmp(session, "wayland") == 0) {
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "x11");
        }
    #endif

    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        printf("SDL_Init error: %s\n", SDL_GetError());
        return -1;
    }
    
    window = SDL_CreateWindow(
        progname,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        printf("SDL_CreateWindow error: %s\n", SDL_GetError());
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        return -1;
    }

    if (SDL_ShowCursor(SDL_ENABLE) < 0)
        printf("error: %s", SDL_GetError());
    
    setup_screen_layout(audio);


    return 0;
}

int setup_audio(struct audio_buffer *audio){
    device_id = SDL_OpenAudioDevice(NULL, 0, &audio->spec, NULL, 0);
    if (!device_id){
        printf("Audio device could not be opened!\n"
                       "SDL_Error: %s\n", SDL_GetError());
        SDL_FreeWAV(audio->buf);
        return -1;
    }

    if (SDL_QueueAudio(device_id, audio->buf, audio->len) < 0){
        printf("Audio could not be queued!\n"
                "SDL_Error: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(device_id);
        SDL_FreeWAV(audio->buf);
        return -1;
    }

    SDL_PauseAudioDevice(device_id, 0);
}

int load_audio(struct audio_buffer *audio){
    SDL_LoadWAV(audio_file_path, &audio->spec, &audio->buf, &audio->len);
    return 0;
}

int setup(void){
    struct audio_buffer audio;
    struct audio_buffer *paudio = &audio;

    if (load_audio(paudio) < 0){
        return -1;
    }

    if (setup_sdl(paudio) < 0){
        return -1;
    }

    if (setup_audio(paudio) < 0){
        return -1;
    }
    return 0;
}

int toggle_audio(){
    SDL_SetRenderDrawColorRGBA(renderer, bkg_pcolor);
    SDL_RenderFillRect(renderer, &r_play);
    if(SDL_GetAudioDeviceStatus(device_id) == SDL_AUDIO_PLAYING){
        SDL_RenderCopy(renderer, play_icon, NULL, &r_play); 
        SDL_PauseAudioDevice(device_id, 1);  
    }else if(SDL_GetAudioDeviceStatus(device_id) == SDL_AUDIO_PAUSED){
        SDL_RenderCopy(renderer, pause_icon, NULL, &r_play); 
        SDL_PauseAudioDevice(device_id, 0);
    }
    
    SDL_RenderPresent(renderer);
}

int mainloop(){
    int window_status = RUNNING;
    while (window_status == RUNNING) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {  
            switch (event.type){
            case SDL_QUIT:
                window_status = STOPPED;
                break;
            case SDL_KEYDOWN:
                    switch (event.key.keysym.sym){
                    case SDLK_SPACE:
                        toggle_audio();
                        break;
                    }
                    break;
            case SDL_MOUSEBUTTONDOWN:
                if (hit_test(&r_play, event.button.x, event.button.y)){
                    DEBUG_PRINTF("hit play button\n");
                    toggle_audio();
                }
            }
        }
        SDL_Delay(16); 
    }

    return 0;
}

void cleanup(void){
    SDL_Quit();
}

int main(int argc, char **argv) {
    if (argc < 2){
        printf("argument needed\n");
        return 1;
    }
    
    progname = argv[0];
    audio_file_path = argv[1];

    if (setup() < 0){
        cleanup();
        return 1;
    }

    mainloop();

    cleanup();

    return 0;
}

float *calculate_peaks(int16_t *samples, int windows, int window_samples){
    float *peaks = calloc(windows, sizeof(float));
    int i, j;
    for (i = 0; i < windows; i++) {
        int start = i * window_samples;
        int end   = start + window_samples;

        int peak = 0;

        for (j = start; j < end; j++) {
            int v = samples[j];
            if (v < 0) 
                v = -v;

            if (v > peak)
                peak = v;
        }

        peaks[i] = peak / 32767.0;
    }
    return peaks;
}

float *calculate_rms(int16_t *samples, int windows, int window_samples) {
    float *rms = calloc(windows, sizeof(float));

    for (int i = 0; i < windows; i++) {

        int start = i * window_samples;
        int end   = start + window_samples;

        double sum = 0.0;

        for (int j = start; j < end; j++) {
            double v = samples[j];
            sum += v * v;
        }

        double mean = sum / window_samples;
        double value = sqrt(mean);

        rms[i] = (float)(value / 32767.0);
    }

    return rms;
}