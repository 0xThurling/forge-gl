#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_pixels.h"
#include "SDL_render.h"
#include "SDL_timer.h"
#include "array.h"
#include "display.h"
#include "light.h"
#include "matrix.h"
#include "mesh.h"
#include "texture.h"
#include "triangle.h"
#include "vector.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <ostream>

/////////////////////////////////////////////////////////////
//// Array of triangle that should be rendered frame by frame
/////////////////////////////////////////////////////////////
triangle_t *triangles_to_render = NULL;

Render_Method render_method = RENDER_WIRE;
Cull_Method cull_method = CULL_BACKFACE;

/////////////////////////////////////////////////////////////
//// Array of triangle that should be rendered frame by frame
/////////////////////////////////////////////////////////////
bool is_running = false;
int previous_frame_time = 0;

vec3_t camera_position = {.x = 0, .y = 0, .z = 0};
mat4_t proj_matrix;

/////////////////////////////////////////////////////////////
//// Array of triangle that should be rendered frame by frame
/////////////////////////////////////////////////////////////
void setup() {
  color_buffer =
      (uint32_t *)malloc(sizeof(uint32_t) * window_width * window_height);

  color_buffer_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           window_width, window_height);

  // Initialize the perspective projection matrix
  float fov = M_PI / 3.0; // 180 deg / 3 = 60 deg
  float aspect = (float)window_height / (float)window_width;
  float znear = 1.0;
  float zfar = 100.0;
  proj_matrix = mat4_make_perspective(fov, aspect, znear, zfar);

  // Manually loaf the texture data form the static array
  mesh_texture = (uint32_t*)REDBRICK_TEXTURE;
  texture_width = 64;
  texture_height = 64;

  // load_obj_file_data("./assets/f22.obj");
  load_cube_mesh_data();
}

/////////////////////////////////////////////////////////////
//// Poll system events and handle keyboard input
/////////////////////////////////////////////////////////////
void process_input() {
  SDL_Event event;
  SDL_PollEvent(&event);
  switch (event.type) {
  case SDL_QUIT:
    is_running = false;
    break;
  case SDL_KEYDOWN:
    if (event.key.keysym.sym == SDLK_ESCAPE) {
      is_running = false;
    }
    if (event.key.keysym.sym == SDLK_1) {
      render_method = RENDER_WIRE_VERTEX;
    }
    if (event.key.keysym.sym == SDLK_2) {
      render_method = RENDER_WIRE;
    }
    if (event.key.keysym.sym == SDLK_3) {
      render_method = RENDER_FILL_TRIANGLE;
    }
    if (event.key.keysym.sym == SDLK_4) {
      render_method = RENDER_FILL_TRIANGLE_WIRE;
    }
    if (event.key.keysym.sym == SDLK_5) {
      render_method = RENDER_TEXTURED;
    }
    if (event.key.keysym.sym == SDLK_6) {
      render_method = RENDER_TEXTURED_WIRE;
    }
    if (event.key.keysym.sym == SDLK_c) {
      cull_method = CULL_BACKFACE;
    }
    if (event.key.keysym.sym == SDLK_d) {
      cull_method = CULL_NONE;
    }
    break;
  }
}

/////////////////////////////////////////////////////////////
//// Update function frame by frame with a fixed time step
/////////////////////////////////////////////////////////////
void update() {
  int time_to_wait = FRAME_TARGET_TIME - (SDL_GetTicks() - previous_frame_time);

  // Only delay execution if we are running too fast
  if (time_to_wait > 0 && time_to_wait <= FRAME_TARGET_TIME) {
    SDL_Delay(time_to_wait);
  }

  previous_frame_time = SDL_GetTicks();

  // Initialize the array of triangle to render
  triangles_to_render = NULL;

  mesh.rotation.x += 0.005;
  mesh.rotation.y += 0.00;
  mesh.rotation.z += 0.00;

  mesh.translation.z = 5.0;

  // Create rotation, translation and scale matrix that will used to multiply
  // the mesh vertices
  mat4_t scale_matrix =
      mat4_make_scale(mesh.scale.x, mesh.scale.y, mesh.scale.z);
  mat4_t translation_matrix = mat4_make_translation(
      mesh.translation.x, mesh.translation.y, mesh.translation.z);

  mat4_t rotation_matrix_x = mat4_make_rotation_x(mesh.rotation.x);
  mat4_t rotation_matrix_y = mat4_make_rotation_y(mesh.rotation.y);
  mat4_t rotation_matrix_z = mat4_make_rotation_z(mesh.rotation.z);

  // Loop all triangle faces of our mesh
  int num_faces = array_length(mesh.faces);

  for (int i = 0; i < num_faces; i++) {
    face_t mesh_face = mesh.faces[i];

    vec3_t face_vertices[3];
    face_vertices[0] = mesh.vertices[mesh_face.a - 1];
    face_vertices[1] = mesh.vertices[mesh_face.b - 1];
    face_vertices[2] = mesh.vertices[mesh_face.c - 1];

    vec4_t transformed_vertices[3];

    // Loop all three vertices of this current face
    // and apply transformations
    for (int j = 0; j < 3; j++) {
      vec4_t transformed_vertex = vec4_from_vec3(face_vertices[j]);

      // Create a world matrix by combining scale, rotation and translation
      // matrices
      mat4_t world_matrix = mat4_identity();
      world_matrix = mat4_mul_mat4(scale_matrix, world_matrix);
      world_matrix = mat4_mul_mat4(rotation_matrix_z, world_matrix);
      world_matrix = mat4_mul_mat4(rotation_matrix_y, world_matrix);
      world_matrix = mat4_mul_mat4(rotation_matrix_x, world_matrix);
      world_matrix = mat4_mul_mat4(translation_matrix, world_matrix);

      transformed_vertex = mat4_mul_vec4(world_matrix, transformed_vertex);

      // Save transformed vertex in the array of the transformed vertices
      transformed_vertices[j] = transformed_vertex;
    }

    // Check the backface culling check
    vec3_t vector_a = vec3_from_vec4(transformed_vertices[0]); // VEC A
    vec3_t vector_b = vec3_from_vec4(transformed_vertices[1]); // VEC B
    vec3_t vector_c = vec3_from_vec4(transformed_vertices[2]); // VEC C

    vec3_t vector_ab = vec3_sub(vector_b, vector_a);
    vec3_t vector_ac = vec3_sub(vector_c, vector_a);

    vec3_t normal = vec3_cross(vector_ab, vector_ac);

    vec3_normalize(&normal);

    vec3_t camera_ray = vec3_sub(camera_position, vector_a);

    float dot_product = vec3_dot(normal, camera_ray);

    if (cull_method == CULL_BACKFACE) {
      if (dot_product <= 0)
        continue;
    }

    vec4_t projected_points[3];

    // Loop all three vertices to perform projection
    for (int j = 0; j < 3; j++) {
      // Project the current vertex
      projected_points[j] =
          mat4_mul_vec4_project(proj_matrix, transformed_vertices[j]);

      // Scale into the view
      projected_points[j].x *= (window_width / 2.0);
      projected_points[j].y *= (window_height / 2.0);

      // Invert the y-values to account for the flipped screen y-coords
      projected_points[j].y *= -1;

      // translate the project point the middle of the seen
      projected_points[j].x += (window_width / 2.0);
      projected_points[j].y += (window_height / 2.0);
    }

    // Calculate the avg Z value of the vertices after the transformation
    float avg_depth = (transformed_vertices[0].z + transformed_vertices[1].z +
                       transformed_vertices[2].z) /
                      3;

    // Calculate the shade intensity on how aligned the face normal is with the
    // inverse of the light ray  
    float light_intensity = -vec3_dot(normal, light.direction);

    // Calculate the triangle color based on the light angle
    uint32_t triangle_color = light_apply_intensity(mesh_face.color, light_intensity);

    triangle_t projected_triangle = {
        .points =
            {
                {projected_points[0].x, projected_points[0].y},
                {projected_points[1].x, projected_points[1].y},
                {projected_points[2].x, projected_points[2].y},
            },
        .texcoords = {
          { mesh_face.a_uv.u, mesh_face.a_uv.v },
          { mesh_face.b_uv.u, mesh_face.b_uv.v },
          { mesh_face.c_uv.u, mesh_face.c_uv.v },
        },
        .color = triangle_color,
        .avg_depth = avg_depth};

    array_push(triangles_to_render, projected_triangle);
  }

  for (int i = 0; i < array_length(triangles_to_render); i++) {
    for (int j = 0; j < array_length(triangles_to_render); j++) {
      if (triangles_to_render[i].avg_depth > triangles_to_render[j].avg_depth) {
        triangle_t tmp = triangles_to_render[i];
        triangles_to_render[i] = triangles_to_render[j];
        triangles_to_render[j] = tmp;
      }
    }
  }
}

/////////////////////////////////////////////////////////////
//// Render function to draw objects on the display
/////////////////////////////////////////////////////////////
void render() {
  clear_color_buffer(0xFF000000);

  // Loop all projected triangles and render them
  int num_triangles = array_length(triangles_to_render);
  for (int i = 0; i < num_triangles; i++) {
    triangle_t triangle = triangles_to_render[i];

    if (render_method == RENDER_FILL_TRIANGLE ||
        render_method == RENDER_FILL_TRIANGLE_WIRE) {
      draw_filled_triangle(triangle.points[0].x, triangle.points[0].y,
                           triangle.points[1].x, triangle.points[1].y,
                           triangle.points[2].x, triangle.points[2].y,
                           triangle.color);
    }

    // Draw texture triangle
    if (render_method == RENDER_TEXTURED || render_method == RENDER_TEXTURED_WIRE){
      draw_textured_triangle(
        triangle.points[0].x, triangle.points[0].y, triangle.texcoords[0].u, triangle.texcoords[0].v,
        triangle.points[1].x, triangle.points[1].y, triangle.texcoords[1].u, triangle.texcoords[1].v,
        triangle.points[2].x, triangle.points[2].y, triangle.texcoords[2].u, triangle.texcoords[2].v,
        mesh_texture);
    }

    if (render_method == RENDER_WIRE || render_method == RENDER_WIRE_VERTEX ||
        render_method == RENDER_FILL_TRIANGLE_WIRE || render_method == RENDER_TEXTURED_WIRE) {
      draw_triangle(triangle.points[0].x, triangle.points[0].y,
                    triangle.points[1].x, triangle.points[1].y,
                    triangle.points[2].x, triangle.points[2].y, 0xFFFFFFFF);
    }

    if (render_method == RENDER_WIRE_VERTEX) {
      // Draw vertex points
      draw_rect(triangle.points[0].x - 3, triangle.points[0].y - 3, 6, 6,
                0xFFFF0000);
      draw_rect(triangle.points[1].x - 3, triangle.points[1].y - 3, 6, 6,
                0xFFFF0000);
      draw_rect(triangle.points[2].x - 3, triangle.points[2].y - 3, 6, 6,
                0xFFFF0000);
    }
  }

  // Clear array of triangles to render every frame
  array_free(triangles_to_render);

  render_color_buffer();

  SDL_RenderPresent(renderer);
}

/////////////////////////////////////////////////////////////
//// Render function to draw objects on the display
/////////////////////////////////////////////////////////////
void free_resources() {
  free(color_buffer);
  array_free(mesh.faces);
  array_free(mesh.vertices);
}

/////////////////////////////////////////////////////////////
//// Render function to draw objects on the display
/////////////////////////////////////////////////////////////
int main() {
  is_running = initialize_window();

  setup();

  while (is_running) {
    process_input();
    update();
    render();
  }

  destroy_window();
  free_resources();

  std::cout << is_running << std::endl;

  return 0;
}
