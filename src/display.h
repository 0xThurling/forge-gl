#pragma once
#ifndef DISPLAY_H
#define DISPLAY_H

#include "SDL_render.h"
#include "SDL_video.h"
#include <cstdint>
#include <SDL.h>

#define FPS 5000
#define FRAME_TARGET_TIME (1000/FPS)


enum Cull_Method {
  CULL_NONE,
  CULL_BACKFACE
};

extern Cull_Method cull_method;

enum Render_Method {
  RENDER_WIRE,
  RENDER_WIRE_VERTEX,
  RENDER_FILL_TRIANGLE,
  RENDER_FILL_TRIANGLE_WIRE,
  RENDER_TEXTURED,
  RENDER_TEXTURED_WIRE
};

extern Render_Method render_method;

extern SDL_Window* window;
extern SDL_Renderer* renderer;
extern uint32_t* color_buffer;
extern SDL_Texture* color_buffer_texture;
extern int window_width;
extern int window_height;

bool initialize_window(void);
void draw_grid(void);
void draw_pixel(int x, int y, uint32_t color);
void draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);
void draw_rect(int x, int y, int width, int height, uint32_t color);
void render_color_buffer(void);
void clear_color_buffer(uint32_t color);
void destroy_window(void);

#endif // DISPLAY_H
