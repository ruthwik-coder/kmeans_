#ifdef _MSC_VER
   #define _CRT_SECURE_NO_WARNINGS
#endif
#define SDL_MAIN_USE_CALLBACKS 1
 #include <SDL3/SDL.h>
 #include <SDL3/SDL_main.h>
 #include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include "kmeans.h"
#define MY_FONT "C:\\Windows\\Fonts\\arial.ttf"
#define SDL_MESSAGEBOX_ERROR                    0x00000010u
#define D 3

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_CameraID* devices;
    SDL_Camera* camera;
    SDL_Texture* texture;
    int K;
    TTF_Font *font ;
    float cam_x, cam_y, cam_w, cam_h;
    bool dragging;
    float drag_offset_x, drag_offset_y;
    int width;
    int height;
    int camera_count;
    Uint8* centroids; // Will store K*3 values (RGB for each centroid)
    // Add these to track texture dimensions
    int texture_width;
    int texture_height;
} AppState ;

SDL_Rect camera_viewport = {20, 20, 320, 240};
bool resizing = false;
int resize_margin = 10;

int rand_lim(int limit) {
    int divisor = RAND_MAX/(limit+1);
    int retval;
    do { 
        retval = rand() / divisor;
    } while (retval > limit);
    return retval;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    if (TTF_Init() < 0) {
        SDL_Log("Couldn't initialize SDL_ttf: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    
    AppState* app_state = malloc(sizeof(AppState));
    *app_state = (AppState){
        .width = 1280,
        .height = 720,
        .texture_width = 0,
        .texture_height = 0,
    };
    *appstate = app_state;
    app_state->K = 2;
    app_state->font = TTF_OpenFont(MY_FONT, 50);
    
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_CAMERA)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    
    app_state->cam_w = app_state->width / 4.0f;
    app_state->cam_h = app_state->height / 4.0f;
    app_state->cam_x = 20.0f;
    app_state->cam_y = 20.0f;
    app_state->dragging = false;
    app_state->centroids = malloc(app_state->K * 3 * sizeof(Uint8));
    
    if (!SDL_CreateWindowAndRenderer("SDL3 Camera Demo", app_state->width, app_state->height, 0, &(app_state->window), &(app_state->renderer))) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_CameraID* devices = SDL_GetCameras(&app_state->camera_count);
    app_state->devices = devices;
    SDL_Log("Found %d cameras!", app_state->camera_count);

    app_state->camera = SDL_OpenCamera(devices[0], NULL);
    if (app_state->camera == NULL) {
        SDL_Log("Can't open the selected camera: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_CameraSpec spec;
    SDL_GetCameraFormat(app_state->camera, &spec);
    int FPS = spec.framerate_numerator / spec.framerate_denominator;
    SDL_Log("Got a camera of %dx%d and FPS %d", spec.width, spec.height, FPS);

    return SDL_APP_CONTINUE;
}
// int compare(const void* a, const void* b) {
//     return (*(int*)a - *(int*)b);
// }
void drawrect(void *appstate) {
    AppState* app_state = (AppState*)appstate;
    int rect_width = 100;
    int rect_height = 100;
    //  qsort(app_state->centroids,(app_state->K)*3, sizeof(Uint8), compare);
    for (int i = 0; i < app_state->K; ++i) {
        SDL_FRect rect = {
            .x = i * rect_width,
            .y = 720-rect_height,
            .w = rect_width,
            .h = rect_height
        };

        Uint8 r = app_state->centroids[i * 3 + 0];
        Uint8 g = app_state->centroids[i * 3 + 1];
        Uint8 b = app_state->centroids[i * 3 + 2];

        SDL_SetRenderDrawColor(app_state->renderer, r, g, b, 255);
        SDL_RenderFillRect(app_state->renderer, &rect);
    }
}

void log_line_error(const char* function_name, int line_number) {
    printf("Error at %s (line %d): %s\n", function_name, line_number, SDL_GetError());
}

// Function to recreate texture when viewport size changes
void recreate_texture_if_needed(AppState* app_state) {
    if (app_state->texture_width != camera_viewport.w || app_state->texture_height != camera_viewport.h) {
        // Destroy old texture
        if (app_state->texture != NULL) {
            SDL_DestroyTexture(app_state->texture);
            printf("Destroyed old texture (%dx%d)\n", app_state->texture_width, app_state->texture_height);
        }
        
        // Create new texture with current viewport dimensions
        app_state->texture = SDL_CreateTexture(app_state->renderer, SDL_PIXELFORMAT_RGB24, 
                                             SDL_TEXTUREACCESS_STREAMING, camera_viewport.w, camera_viewport.h);
        
        if (app_state->texture == NULL) {
            log_line_error(__func__, __LINE__);
        } else {
            app_state->texture_width = camera_viewport.w;
            app_state->texture_height = camera_viewport.h;
            printf("Created new texture (%dx%d)\n", app_state->texture_width, app_state->texture_height);
        }
    }
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* app_state = (AppState*)appstate;
    
    SDL_Surface* frame = SDL_AcquireCameraFrame(app_state->camera, NULL);
    if (frame != NULL) {
        // Always check if we need to recreate texture due to viewport size changes
        recreate_texture_if_needed(app_state);
        
        if (app_state->texture == NULL) {
            SDL_Log("Failed to create/recreate texture");
            SDL_ReleaseCameraFrame(app_state->camera, frame);
            return SDL_APP_CONTINUE;
        }

        // Convert and scale frame to current viewport dimensions
        SDL_Surface* aa = SDL_ConvertSurface(frame, SDL_PIXELFORMAT_RGB24);
        SDL_Surface* rgb_frame = SDL_ScaleSurface(aa, camera_viewport.w, camera_viewport.h, SDL_SCALEMODE_NEAREST);
        SDL_DestroySurface(aa);
        
        int N = rgb_frame->w * rgb_frame->h;
        
        if (rgb_frame) {
            int *Location = (int*)calloc(N + (D * app_state->K), sizeof(int));
            Uint8* pixels = (Uint8*)rgb_frame->pixels;
            int pitch = rgb_frame->pitch;
            int step = 1;
            
            Location = kmeans2(pixels, Location, N, D, app_state->K);

            // Extract centroids from the end of Location array and store them
            for (int k = 0; k < app_state->K; k++) {
                int centroid_base = N + k * D;
                app_state->centroids[k * 3 + 0] = (Uint8)Location[centroid_base + 0]; // R
                app_state->centroids[k * 3 + 1] = (Uint8)Location[centroid_base + 1]; // G
                app_state->centroids[k * 3 + 2] = (Uint8)Location[centroid_base + 2]; // B
            }

            for (int y = 0; y < rgb_frame->h; y += step) {
                for (int x = 0; x < rgb_frame->w; x += step) {
                    int pixel_index = y * pitch + x * 3; // 3 for RGB24
                    int cluster = Location[pixel_index / 3]; // 0..K-1

                    // Use the stored centroid colors
                    pixels[pixel_index + 0] = app_state->centroids[cluster * 3 + 0]; // R
                    pixels[pixel_index + 1] = app_state->centroids[cluster * 3 + 1]; // G
                    pixels[pixel_index + 2] = app_state->centroids[cluster * 3 + 2]; // B
                }
            }
            free(Location);

            SDL_UpdateTexture(app_state->texture, NULL, rgb_frame->pixels, rgb_frame->pitch);
            SDL_DestroySurface(rgb_frame);
        }

        SDL_ReleaseCameraFrame(app_state->camera, frame);
    } else {
        return SDL_APP_CONTINUE;
    }
    
    SDL_SetRenderDrawColorFloat(app_state->renderer, 0.3f, 0.5f, 1.0f, SDL_ALPHA_OPAQUE_FLOAT);
    SDL_RenderClear(app_state->renderer);

    SDL_FRect camera_viewport_f = {
        .x = (float)camera_viewport.x,
        .y = (float)camera_viewport.y,
        .w = (float)camera_viewport.w,
        .h = (float)camera_viewport.h
    };

    SDL_RenderTexture(app_state->renderer, app_state->texture, NULL, &camera_viewport_f);
    SDL_SetRenderDrawColor(app_state->renderer, 20, 20, 20, 255);

    // Render k value
    char cv[32];
    snprintf(cv, sizeof(cv), "k=%d", app_state->K);
    SDL_Color textColor = {0, 0, 0, 0};
    SDL_Surface* kSurface = TTF_RenderText_Blended(app_state->font, cv, strlen(cv), textColor);
    SDL_Texture* kTexture = SDL_CreateTextureFromSurface(app_state->renderer, kSurface);
    SDL_FRect kRect = {700, 50, 50, 50};

    SDL_RenderTexture(app_state->renderer, kTexture, NULL, &kRect);
    SDL_DestroyTexture(kTexture);
    SDL_DestroySurface(kSurface);

    drawrect(appstate);
    SDL_RenderPresent(app_state->renderer);
    
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    AppState* app_state = (AppState*)appstate;
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }


    if (event->type == SDL_EVENT_KEY_DOWN) { 
        if (event->key.scancode == SDL_SCANCODE_KP_PLUS) {
            app_state->K++;
            app_state->centroids = realloc(app_state->centroids, app_state->K * 3 * sizeof(Uint8));
            printf("K increased to: %d\n", app_state->K);
        } else if (event->key.scancode == SDL_SCANCODE_KP_MINUS) {
            if (app_state->K > 1) {
                app_state->K--;
                app_state->centroids = realloc(app_state->centroids, app_state->K * 3 * sizeof(Uint8));
                printf("K decreased to: %d\n", app_state->K);
            }
        } else if (event->key.scancode == SDL_SCANCODE_M) {
            camera_viewport.w += 20;  // Increase width by 20 pixels
            printf("Viewport width increased to: %d\n", camera_viewport.w);
        } else if (event->key.scancode == SDL_SCANCODE_N) {
            camera_viewport.h += 20;  // Increase height by 20 pixels
            printf("Viewport height increased to: %d\n", camera_viewport.h);
        } else if (event->key.scancode == SDL_SCANCODE_J) {
            if (camera_viewport.w > 50) {  // Minimum width
                camera_viewport.w -= 20;  // Decrease width by 20 pixels
                printf("Viewport width decreased to: %d\n", camera_viewport.w);
            }
        } else if (event->key.scancode == SDL_SCANCODE_K) {
            if (camera_viewport.h > 50) {  // Minimum height
                camera_viewport.h -= 20;  // Decrease height by 20 pixels
                printf("Viewport height decreased to: %d\n", camera_viewport.h);
            }
        }
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    AppState* app_state = (AppState*)appstate;
    if (app_state->devices != NULL) {
        SDL_free(app_state->devices);
    }
    if (app_state->camera != NULL) {
        SDL_CloseCamera(app_state->camera);
    }
    if (app_state->texture != NULL) {
        SDL_DestroyTexture(app_state->texture);
    }
    if (app_state->centroids != NULL) {
        free(app_state->centroids);
    }
    free(app_state);
}