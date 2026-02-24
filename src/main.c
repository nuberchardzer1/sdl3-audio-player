#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> 
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

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

static SDL_FRect r_menu;
static int menu_padding_x = 100;
static int menu_padding_y = 20;
static int menu_height = 100; 

static SDL_FRect r_timeline;
static int timeline_padding_x = 50;
static int timeline_padding_y = 20;

static SDL_FRect r_timelinebar;
static int timelinebar_width = 5;

static SDL_FRect r_timelinebtn;
static int timelinebtn_size = 12;

static int time_counters_padding_y = 10;
static int time_counters_width = 35;
static int time_counters_height = 20;
static SDL_FRect r_time_left;
static SDL_FRect r_time_remaining;

TTF_Font *ttf_font = NULL; 
TTF_TextEngine *ttf_engine = NULL;
static int ttf_font_size = 20;

static SDL_FRect r_volumebar;
static int volumebar_padding_x = 50;
static int volumebar_length = 90;
static int volumebar_width = 5;

static SDL_FRect r_volumebtn;
static int volumebtn_size = 10;

static SDL_FRect r_play;
static int play_btn_height = 50; 
static int play_btn_weight = 50; 

static SDL_Texture *pause_icon = NULL;
static SDL_Texture *play_icon = NULL;

static int WINDOW_WIDTH = 800;
static int WINDOW_HEIGHT = 600;

static char *progname;
static char *audio_file_path;

static SDL_AudioStream *stream = NULL;

static SDL_AudioDeviceID audio_devid;

struct audio_buffer{
    Uint32 len;
    Uint8 *buf;
    Uint32 pos;
    SDL_AudioSpec spec; 
};

pthread_mutex_t audio_mu = PTHREAD_MUTEX_INITIALIZER;
struct audio_buffer *audio = NULL;

static float *rms = NULL;

#define background_color 209, 229, 244, 255
#define graphic_color 22, 30, 26, 255
#define time_counters_color 0, 0, 0, 255

/** * Test whether an x/y coordinate is within a given rect. * 
 * 
 * @param r The rect 
 * @param x X coordinate 
 * @param y Y coordinate 
 * @return true if a hit, else false */ 
static int hit_rect(const SDL_FRect *const r, unsigned x, unsigned y){ 
    /* note the use of unsigned math: no need to check for negative */ 
    return (x - (unsigned)r->x < (unsigned)r->w) && (y - (unsigned)r->y < (unsigned)r->h); } 


float *calculate_peaks(int16_t *samples, int windows, int window_samples);
float *calculate_rms(int16_t *samples, int windows, int window_samples); 
int SDL_RenderFillCircle(SDL_Renderer * renderer, int x, int y, int radius);


/**
 * audio_track_time
 *
 * Snapshot of playback timing in whole seconds:
 * - elapsed_sec:   seconds played since the start (0..total_sec)
 * - remaining_sec: seconds left until the end (0..total_sec), typically for countdown UI
 * - total_sec:     total duration in seconds
 */
typedef struct audio_track_time {
    int elapsed_sec;
    int remaining_sec;
    int total_sec;
} audio_track_time;


//The drag_kind enum represent drag on a different buttons  
typedef enum {
    DRAG_NONE = 0,
    DRAG_TIMELINE,
    DRAG_VOLUME
} drag_kind;

static audio_track_time calculate_audio_track_time(const struct audio_buffer *a);

void render_audio_graphic(){
    const int padding = 150;

    const int bar_width = 2;
    const int gap = 1;
    
    int graphic_lines = WINDOW_WIDTH / (bar_width + gap);
    int offset = bar_width + gap;

    int total_samples = audio->len / sizeof(int16_t);
    int window_samples = total_samples / graphic_lines;

    int windows = graphic_lines;

    int16_t *samples = (int16_t *)audio->buf;

    if (!rms){
        rms = calculate_rms(samples, windows, window_samples); 
    }

    SDL_SetRenderDrawColor(renderer, graphic_color);

    int xpos = 0; 
    const int y1 = padding;
    const int y2 = WINDOW_HEIGHT - padding;
    const int max_line_length = y2 - y1;

    for (int i = 0; i < windows; i++){
        float coeff = rms[i];
        // printf("peak %d --> %0.2f\n", i, coeff);
        int line_padding = (int)(max_line_length * (1 - coeff) / 3);
        SDL_RenderLine(renderer, xpos, y1 + line_padding, xpos, y2 - line_padding);
        xpos += offset;
    }
}

void render_screen(){
    SDL_SetRenderDrawColor(renderer, background_color);
    SDL_RenderClear(renderer);

    render_audio_graphic();
    
    //draw timeline 
    SDL_RenderFillRect(renderer, &r_timelinebar);
    
    int cx = r_timelinebtn.x + r_timelinebtn.w / 2;
    int cy = r_timelinebar.y + r_timelinebar.h / 2;
    int radius = timelinebtn_size / 2;
    SDL_RenderFillCircle(renderer, cx, cy, radius);
    // SDL_RenderFillRect(renderer, &r_timelinebtn);

    pthread_mutex_lock(&audio_mu);
    audio_track_time time = calculate_audio_track_time(audio);
    pthread_mutex_unlock(&audio_mu);

    char time_elapsed_text[16];
    char time_remaining_text[16];

    snprintf(time_elapsed_text, sizeof time_elapsed_text, "%d:%02d",
         time.elapsed_sec / 60, time.elapsed_sec % 60);

    snprintf(time_remaining_text, sizeof time_remaining_text, "%d:%02d",
         time.remaining_sec / 60, time.remaining_sec % 60);

    TTF_Text *txt_elapsed = TTF_CreateText(ttf_engine, ttf_font, time_elapsed_text, strlen(time_elapsed_text));
    TTF_Text *txt_remaining = TTF_CreateText(ttf_engine, ttf_font, time_remaining_text, strlen(time_remaining_text));
    if (!txt_elapsed || !txt_remaining) {
        fprintf(stderr, "TTF_CreateText failed: %s\n", SDL_GetError());
    }

    TTF_SetTextColor(txt_elapsed, time_counters_color);
    TTF_SetTextColor(txt_remaining, time_counters_color);

    TTF_DrawRendererText(txt_elapsed, r_time_left.x, r_time_left.y);

    TTF_DrawRendererText(txt_remaining, r_time_remaining.x, r_time_remaining.y);

    SDL_free(txt_elapsed);
    SDL_free(txt_remaining);
    //draw volume control
    SDL_RenderFillRect(renderer, &r_volumebar);

    cx = r_volumebtn.x + r_volumebtn.w / 2;
    cy = r_volumebar.y + r_volumebar.h / 2;
    radius = volumebtn_size / 2;
    SDL_RenderFillCircle(renderer, cx, cy, radius);

    if (SDL_AudioStreamDevicePaused(stream)) {
        SDL_RenderTexture(renderer, play_icon, NULL, &r_play); 
    } else {
        SDL_RenderTexture(renderer, pause_icon, NULL, &r_play); 
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    SDL_RenderPresent(renderer);
}

static inline int center_y(int y, int h, int inner_h) {
    return y + (h - inner_h) / 2;
}

void setup_menu(){
    // MENU
    r_menu.x = menu_padding_x;
    r_menu.y = WINDOW_HEIGHT - menu_padding_y - menu_height;
    r_menu.w = WINDOW_WIDTH - menu_padding_x * 2;
    r_menu.h = menu_height;

    // TIMELINE AREA (top half)
    r_timeline.x = r_menu.x + timeline_padding_x;
    r_timeline.y = r_menu.y;
    r_timeline.w = r_menu.w - timeline_padding_x * 2;
    r_timeline.h = r_menu.h / 2;

    // TIMELINE BAR
    r_timelinebar.x = r_timeline.x;
    r_timelinebar.w = r_timeline.w;
    r_timelinebar.h = timelinebar_width;
    r_timelinebar.y = r_timeline.y + (r_timeline.h - r_timelinebar.h) / 2;

    // TIMELINE BUTTON
    r_timelinebtn.w = timelinebtn_size;
    r_timelinebtn.h = timelinebtn_size;
    r_timelinebtn.x = r_timelinebar.x;
    r_timelinebtn.y = r_timelinebar.y + (r_timelinebar.h - r_timelinebtn.h) / 2;

    r_time_left.x = r_timelinebar.x;
    r_time_left.y = r_timelinebar.y + r_timelinebar.h + time_counters_padding_y;
    r_time_left.w = time_counters_width;
    r_time_left.h = time_counters_height;

    r_time_remaining.x = r_timelinebar.x + r_timelinebar.w - time_counters_width;
    r_time_remaining.y = r_timelinebar.y + r_timelinebar.h + time_counters_padding_y;
    r_time_remaining.w = time_counters_width;
    r_time_remaining.h = time_counters_height;

    // CONTROLS AREA (bottom half)
    int controls_y = r_menu.y + r_menu.h / 2;
    int controls_h = r_menu.h - r_menu.h / 2;

    // PLAY (center)
    r_play.w = play_btn_weight;
    r_play.h = play_btn_height;
    r_play.x = r_menu.x + r_menu.w / 2 - r_play.w / 2;
    r_play.y = controls_y + (controls_h - r_play.h) / 2;

    // VOLUME BAR 
    r_volumebar.h = volumebar_width;
    r_volumebar.x = r_menu.x + r_menu.w - volumebar_length - volumebar_padding_x;
    r_volumebar.w = volumebar_length;
    r_volumebar.y = controls_y + (controls_h - r_volumebar.h) / 2;

    // VOLUME BUTTON
    r_volumebtn.w = volumebtn_size;
    r_volumebtn.h = volumebtn_size;
    r_volumebtn.x = r_volumebar.x + r_volumebar.w - r_volumebtn.w;  
    r_volumebtn.y = r_volumebar.y + (r_volumebar.h - r_volumebtn.h) / 2;
}

int setup_sdl(){
    #if defined(__linux__)
        const char *session = getenv("XDG_SESSION_TYPE");
        if (session && strcmp(session, "wayland") == 0) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
        }
    #endif

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_CreateWindowAndRenderer(progname, WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer);
    if (!window) {
        printf("SDL_CreateWindow error: %s\n", SDL_GetError());
        return -1;
    }

    if (!renderer) {
        printf("SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        return -1;
    }

    if (SDL_ShowCursor() < 0)
        printf("error: %s", SDL_GetError());
    
    // init static
    pause_icon = IMG_LoadTexture(renderer, "assets/img/pause_icon.png");
    if (!pause_icon){
        printf("Error: IMG_LoadTexture failed (pause_icon): %s\n", SDL_GetError());
    }
    SDL_SetTextureScaleMode(pause_icon, SDL_SCALEMODE_LINEAR);
    
    play_icon = IMG_LoadTexture(renderer, "assets/img/play_icon.png");
    if (!play_icon){
        printf("Error: IMG_LoadTexture failed (play_icon): %s\n", SDL_GetError());
    }   
    SDL_SetTextureScaleMode(play_icon, SDL_SCALEMODE_LINEAR);
    if (!TTF_Init()) {
        fprintf(stderr, "TTF_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    ttf_font = TTF_OpenFont("assets/font/claimcheck.ttf", ttf_font_size);
    if (!ttf_font){
        printf("Error: TTF_OpenFont failed: %s\n", SDL_GetError());
    }   
    
    ttf_engine = TTF_CreateRendererTextEngine(renderer);
    if (!ttf_engine){
        printf("Error: TTF_CreateRendererTextEngine failed: %s\n", SDL_GetError());
    }  

    setup_menu();
    return 0;
}

int audio_buffer_init(){
    if (!audio || !audio_file_path) {
        fprintf(stderr, "audio or audio_file_path is NULL\n");
        return -1;
    }

    if (!SDL_LoadWAV(audio_file_path, &audio->spec, &audio->buf, &audio->len)) {
        fprintf(stderr, "SDL_LoadWAV failed: %s\n", SDL_GetError());
        return -1;
    }

    audio->pos = 0;
    return 0;
}

static void audio_callback(
    void *userdata, 
    SDL_AudioStream *stream, 
    int additional_amount, 
    int total_amount
){  
    // DEBUG_PRINTF("additional_amount: %d, total_amount: %d\n", additional_amount, total_amount);
    pthread_mutex_lock(&audio_mu);

    const int CHUNK_SIZE = 512;
    if (audio->pos + CHUNK_SIZE > audio->len){
        DEBUG_PRINTF("not enough data in audio buffer\n");
        pthread_mutex_unlock(&audio_mu);
        return;
    }
    while (additional_amount > 0){
        SDL_PutAudioStreamData(stream, audio->buf + audio->pos, CHUNK_SIZE);
        audio->pos += CHUNK_SIZE;
        additional_amount -= CHUNK_SIZE;
    }
    pthread_mutex_unlock(&audio_mu);
}

int setup_audio(){
    audio = malloc(sizeof(struct audio_buffer));
    audio_buffer_init();
    
    audio_devid = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio->spec);
    if (!audio_devid){
        printf("Audio device could not be opened!\n"
                       "SDL_Error: %s\n", SDL_GetError());
        SDL_free(audio->buf);
        return -1;
    }
    
    stream = SDL_OpenAudioDeviceStream(audio_devid, &audio->spec, audio_callback, &audio);
    if (stream == NULL)
        printf("Uhoh, stream failed to create: %s\n", SDL_GetError());
    

    SDL_ResumeAudioStreamDevice(stream);
}


int setup(void){
    if (setup_sdl() < 0)
        return -1;
    if (setup_audio() < 0)
        return -1;

    return 0;
}

int toggle_audio(){
    if (SDL_AudioStreamDevicePaused(stream)) {
        DEBUG_PRINTF("device resumed\n");
        if (!SDL_ResumeAudioStreamDevice(stream)) {
            fprintf(stderr, "Resume failed: %s\n", SDL_GetError());
        }
    } else {
        DEBUG_PRINTF("device paused\n");
        if (!SDL_PauseAudioStreamDevice(stream)) {
            fprintf(stderr, "Pause failed: %s\n", SDL_GetError());
        }
    }
}

void seek_audio(float percent){
    pthread_mutex_lock(&audio_mu);
    audio->pos = audio->len * percent;
    int bytes_per_sample = SDL_AUDIO_BITSIZE(audio->spec.format) / 8;
    int bytes_per_frame  = bytes_per_sample * audio->spec.channels;
    audio->pos -= audio->pos % bytes_per_frame;
    pthread_mutex_unlock(&audio_mu);
}

void adjust_volume(float gain){
   SDL_SetAudioStreamGain(stream, gain);
}

float drag_slider_x(SDL_FRect* btn, const SDL_FRect* bar, float mouse_x){
    btn->x = mouse_x;
            
    //BOUNDS CHECK
    int min = bar->x;
    int max = bar->x + bar->w - btn->w;
    //TOP
    if (btn->x <= bar->x)
        btn->x = min;
    //BOT
    if (btn->x+btn->w >= bar->x + bar->w)
        btn->x = max;

    float slider_pos = (btn->x - min) / (float)(max - min); // 0..1
    DEBUG_PRINTF("%0.2f\n", slider_pos);
    return slider_pos;
}

void update_ui_from_audio(drag_kind drag){
    if (drag != DRAG_TIMELINE){
        pthread_mutex_lock(&audio_mu);
        float track_percent = (audio->len > 0) ? ((float)audio->pos / (float)audio->len) : 0.0f;
        pthread_mutex_unlock(&audio_mu);

        int minx = r_timelinebar.x;
        int maxx = r_timelinebar.x + r_timelinebar.w - r_timelinebtn.w;
        r_timelinebtn.x = minx + (int)((maxx - minx) * track_percent);
    }
}

int mainloop(){
    float slider_pos;
    drag_kind drag = DRAG_NONE;
    SDL_FPoint mouse;
    
    int window_status = RUNNING;
    while (window_status == RUNNING) {
        SDL_Event event;
        SDL_GetMouseState(&mouse.x, &mouse.y);

        while (SDL_PollEvent(&event)) {  
            switch (event.type){
            case SDL_EVENT_QUIT:
                window_status = STOPPED;
                break;
            case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key){
                    case SDLK_SPACE:
                        toggle_audio();
                        break;
                    }
                    break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (hit_rect(&r_play, event.button.x, event.button.y)){
                    DEBUG_PRINTF("hit play button\n");
                    toggle_audio();
                }else if (hit_rect(&r_timelinebtn, mouse.x, mouse.y)
            && event.button.button == SDL_BUTTON_LEFT){
                    drag=DRAG_TIMELINE;
                }else if (hit_rect(&r_volumebtn, mouse.x, mouse.y)
            && event.button.button == SDL_BUTTON_LEFT){
                    drag=DRAG_VOLUME;
            }
                break;
            
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (drag == DRAG_TIMELINE)
                    seek_audio(slider_pos);
                if (event.button.button==SDL_BUTTON_LEFT)
                    drag = DRAG_NONE;
                break;
            
            case SDL_EVENT_MOUSE_MOTION:
                switch (drag){
                case DRAG_TIMELINE:
                    slider_pos = drag_slider_x(&r_timelinebtn, &r_timelinebar, mouse.x);
                    break;
                case DRAG_VOLUME:
                    slider_pos = drag_slider_x(&r_volumebtn, &r_volumebar, mouse.x);
                    adjust_volume(slider_pos);
                    break;
                }
            }
        }

        //update timeline btn position if btn not dragged
        update_ui_from_audio(drag);

        render_screen();
        SDL_Delay(16); 
    }
    return 0;
}

void cleanup(void){
    SDL_free(audio->buf);
    SDL_Quit();
}

int main(int argc, char **argv) {
    if (argc < 2){
        printf("Error: No .wav file specified.\n");
        printf("Usage: ./audio_player /full/path/to/your/audio.wav\n");
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

int SDL_RenderFillCircle(SDL_Renderer * renderer, int x, int y, int radius){
    int offsetx, offsety, d;
    int status;

    offsetx = 0;
    offsety = radius;
    d = radius -1;
    status = 0;

    while (offsety >= offsetx) {
        status += SDL_RenderLine(renderer, x - offsety, y + offsetx,
                                     x + offsety, y + offsetx);
        status += SDL_RenderLine(renderer, x - offsetx, y + offsety,
                                     x + offsetx, y + offsety);
        status += SDL_RenderLine(renderer, x - offsetx, y - offsety,
                                     x + offsetx, y - offsety);
        status += SDL_RenderLine(renderer, x - offsety, y - offsetx,
                                     x + offsety, y - offsetx);

        if (status < 0) {
            status = -1;
            break;
        }

        if (d >= 2*offsetx) {
            d -= 2*offsetx + 1;
            offsetx +=1;
        }
        else if (d < 2 * (radius - offsety)) {
            d += 2 * offsety - 1;
            offsety -= 1;
        }
        else {
            d += 2 * (offsety - offsetx - 1);
            offsety -= 1;
            offsetx += 1;
        }
    }

    return status;
}

static audio_track_time calculate_audio_track_time(const struct audio_buffer *a){
    audio_track_time t = {0, 0, 0};

    const int bytes_per_sample = SDL_AUDIO_BITSIZE(a->spec.format) / 8;
    const int bytes_per_frame  = bytes_per_sample * a->spec.channels;

    if (bytes_per_frame <= 0 || a->spec.freq <= 0 || a->len == 0) {
        return t;
    }

    Uint32 pos = a->pos;
    if (pos > a->len) pos = a->len;

    const Uint64 total_frames   = (Uint64)a->len / (Uint64)bytes_per_frame;
    const Uint64 elapsed_frames = (Uint64)pos     / (Uint64)bytes_per_frame;

    const Uint64 total_ms   = total_frames   * 1000ULL / (Uint64)a->spec.freq;
    const Uint64 elapsed_ms = elapsed_frames * 1000ULL / (Uint64)a->spec.freq;
    const Uint64 clamped_elapsed_ms = (elapsed_ms > total_ms) ? total_ms : elapsed_ms;

    const Uint64 remaining_ms = total_ms - clamped_elapsed_ms;

    t.total_sec = (int)(total_ms / 1000ULL);
    t.elapsed_sec = (int)(clamped_elapsed_ms / 1000ULL);

    // For countdown UIs it's nicer to round up remaining time, so it hits 0 at the end.
    t.remaining_sec = (int)((remaining_ms + 999ULL) / 1000ULL);

    // Safety clamps
    if (t.elapsed_sec < 0) t.elapsed_sec = 0;
    if (t.total_sec < 0) t.total_sec = 0;
    if (t.elapsed_sec > t.total_sec) t.elapsed_sec = t.total_sec;
    if (t.remaining_sec < 0) t.remaining_sec = 0;
    if (t.remaining_sec > t.total_sec) t.remaining_sec = t.total_sec;

    return t;
}
