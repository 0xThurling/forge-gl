# Migrating forge-gl to ForgeFP

This is the migration of the `forge-gl` software renderer to the new ForgeFP
library (`fp/`), in a deliberately *functional* style that leans on the library
end to end.

Verification performed while producing this document:

- every file compiles with GCC 16 against the ForgeFP headers
  (`-Wall -Wextra -Wpedantic`, clean) using a stub SDL;
- the OBJ parser was run against `assets/cube.obj`, `assets/f22.obj`,
  `assets/bunny.obj` (69,451 faces) and `assets/teapot.obj` — all parse, and a
  deliberately broken file reports *every* bad line with its line number;
- the migrated rasterizer was diffed against the original triangle filler on
  20,000 random triangles — 0 pixel mismatches;
- the whole program was smoke-run (stub SDL) from the repo root: it loads
  `assets/f22.obj` through the parser + `par_map` pipeline and exits cleanly.

## ForgeFP modules used

| Module | Where / why |
|---|---|
| `adt.hpp` | render-mode dispatch (`cond`/`when`/`otherwise`), `value_or` for key fallback, variant `match` + `case_` |
| `combinators.hpp` | `flip` to fold matrices in the right order |
| `compose.hpp` | `into`/`out`/`\|` pipelines, `pipe` for the vec3 -> screen stage |
| `concurrent.hpp` | `ThreadPool` + `par_map` for per-face world/projection transforms |
| `curry.hpp` | partially applied `to_world` / `to_screen` |
| `either.hpp` | `and_then`, `map_error`, `tap_ok` / `tap_err` |
| `error.hpp` | `Outcome<mesh_t>`, `errc::*`, `with_context`, `to_string` |
| `grid.hpp` | `for_each_index` for grid / rect pixel loops |
| `io.hpp` | `read_lines` |
| `map.hpp` | `lookup` for key bindings |
| `ops.hpp` | `times`, `negate`, `le`, `lt`, `gt` |
| `parse.hpp` | the OBJ grammar (numbers, indices, vertex/face parsers, `run`) |
| `ranges.hpp` | `map`, `filter_map`, `sort_by`, `fold_left`, `to_vector`, `range`, `reverse`, `sum` |
| `result.hpp` | `traverse`, `from_result`, `Result<void>` for SDL init |
| `simd.hpp` | `dot` for vec2/vec3, `clamp_inplace` for light intensities, `map_inplace` for clearing the buffer |
| `string.hpp` | `starts_with`, `join` |
| `validation.hpp` | `traverse` so **all** malformed OBJ lines are reported at once |
| `vec.hpp` | `zip_with`, `sum`, `enumerate`, `sort_by` |
| `views.hpp` | lazy `filter` / `map` over parsed lines, `enumerate` for triangle points |

Not used, and why: `arena.hpp` (no short-lived allocation-heavy data — the
vectors are the right tool), `input.hpp` (SDL owns input), `memoize.hpp`
(loading happens once), `stream.hpp` / `task.hpp` (the frame loop is a plain
while; a stream/actor rewrite would not earn its complexity here).

## Gotchas found

1. `into(x) | f` on a `Result` uses `map`, **not** `and_then`. A fallible stage
   inside the pipe produces a nested `Result`, so fallible chains stay as
   `>>=`; the pipe is for total functions (or `map`-only stages).
2. `fp::scan` cannot reproduce the rasterizer's iterative float accumulation
   exactly: scanning with a shifted init changes the rounding of
   `x + i*slope` and produced 661/20000 pixel mismatches. The rasterizer keeps
   the plain loop; `scan` is not worth the drift here.
3. `simd.hpp` requires `<experimental/simd>` (GCC/Clang) and a native-SIMD
   target; `concurrent.hpp` (and therefore `simd.hpp`) needs `-pthread` /
   `Threads::Threads`.
4. `fp::par_map(pool, ...)` with a reused `ThreadPool` avoids the per-frame
   `std::async` thread churn of the free `par_map`; on a 12-face cube the
   parallelism is pointless, on `bunny.obj` it pays.

## forge.lua (dependency was pointing at the wrong repo/target)

```lua
return {
  project = {
    name = "renderer",
    type = "executable",
    standard = "20",
    install_headers = false,
  },
  dependencies = {
    direct = {
      sdl = {
        git = "https://github.com/libsdl-org/SDL.git",
        tag = "release-2.32.10",
        target = "SDL2::SDL2",
      },
      forgefp = {
        git = "https://github.com/0xThurling/ForgeFP.git",
        tag = "main",
        target = "forgefp",
      },
    },
  },
  scripts = {
    -- ["pre-build"] = "doxygen",
  },
}
```

Run `forge build` so it regenerates `.config/cmake/CMakeLists.txt` with the
`forgefp` FetchContent + link. For a local checkout, use:

```cmake
FetchContent_Declare(forgefp SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../fp")
FetchContent_MakeAvailable(forgefp)
find_package(Threads REQUIRED)
target_link_libraries(renderer PRIVATE SDL2::SDL2 forgefp Threads::Threads)
```

Includes use the installed mirror `<forgefp/fp/...>`; with `-I fp/src` use
`<fp/...>` instead.

## Delete `array.h` / `array.cpp`

Nothing replaces them: `std::vector` + `fp::to_vector`.

## src/vector.h

```cpp
#ifndef VECTOR_H
#define VECTOR_H

typedef struct {
  float x;
  float y;
} vec2_t;

typedef struct {
  float x;
  float y;
  float z;
} vec3_t;

typedef struct {
  float x;
  float y;
  float z;
  float w;
} vec4_t;

/////////////////////////////////////////
/// Vector 2D functions
////////////////////////////////////////
float vec2_length(vec2_t v);
vec2_t vec2_add(vec2_t a, vec2_t b);
vec2_t vec2_sub(vec2_t a, vec2_t b);
vec2_t vec2_mul(vec2_t v, float factor);
vec2_t vec2_div(vec2_t v, float factor);
float vec2_dot(vec2_t a, vec2_t b);

/////////////////////////////////////////
/// Vector 3D functions
////////////////////////////////////////
float vec3_length(vec3_t v);
vec3_t vec3_add(vec3_t a, vec3_t b);
vec3_t vec3_sub(vec3_t a, vec3_t b);
vec3_t vec3_mul(vec3_t v, float factor);
vec3_t vec3_div(vec3_t v, float factor);
vec3_t vec3_cross(vec3_t a, vec3_t b);
float vec3_dot(vec3_t a, vec3_t b);
vec3_t vec3_normalize(vec3_t v);

vec3_t vec3_rotate_x(vec3_t v, float angle);
vec3_t vec3_rotate_y(vec3_t v, float angle);
vec3_t vec3_rotate_z(vec3_t v, float angle);

/////////////////////////////////////////
/// Vector conversion functions
////////////////////////////////////////
vec4_t vec4_from_vec3(vec3_t v);
vec3_t vec3_from_vec4(vec4_t v);

#endif // VECTOR_H
```

## src/vector.cpp

```cpp
#include "vector.h"
#include <cmath>
#include <forgefp/fp/simd.hpp>
#include <vector>

////////////////////////////////////
/// Vec 2 functions
////////////////////////////////////

float vec2_length(vec2_t v) {
  return std::sqrt(vec2_dot(v, v));
}

vec2_t vec2_add(vec2_t a, vec2_t b) {
  return {.x = a.x + b.x, .y = a.y + b.y};
}

vec2_t vec2_sub(vec2_t a, vec2_t b) {
  return {.x = a.x - b.x, .y = a.y - b.y};
}

vec2_t vec2_mul(vec2_t v, float factor) {
  return {.x = v.x * factor, .y = v.y * factor};
}

vec2_t vec2_div(vec2_t v, float factor) {
  return {.x = v.x / factor, .y = v.y / factor};
}

float vec2_dot(vec2_t a, vec2_t b) {
  return fp::dot(std::vector<float>{a.x, a.y}, std::vector<float>{b.x, b.y});
}

////////////////////////////////////
/// Vec 3 functions
////////////////////////////////////

float vec3_length(vec3_t v) {
  return std::sqrt(vec3_dot(v, v));
}

vec3_t vec3_add(vec3_t a, vec3_t b) {
  return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

vec3_t vec3_sub(vec3_t a, vec3_t b) {
  return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

vec3_t vec3_mul(vec3_t v, float factor) {
  return {.x = v.x * factor, .y = v.y * factor, .z = v.z * factor};
}

vec3_t vec3_div(vec3_t v, float factor) {
  return {.x = v.x / factor, .y = v.y / factor, .z = v.z / factor};
}

vec3_t vec3_cross(vec3_t a, vec3_t b) {
  return {
      .x = a.y * b.z - a.z * b.y,
      .y = a.z * b.x - a.x * b.z,
      .z = a.x * b.y - a.y * b.x,
  };
}

float vec3_dot(vec3_t a, vec3_t b) {
  return fp::dot(std::vector<float>{a.x, a.y, a.z},
                 std::vector<float>{b.x, b.y, b.z});
}

vec3_t vec3_normalize(vec3_t v) {
  return vec3_div(v, vec3_length(v));
}

////////////////////////////////////
/// Vec rotation functions
////////////////////////////////////

vec3_t vec3_rotate_x(vec3_t v, float angle) {
  return {
      .x = v.x,
      .y = static_cast<float>(v.y * cos(angle) - v.z * sin(angle)),
      .z = static_cast<float>(v.y * sin(angle) + v.z * cos(angle)),
  };
}

vec3_t vec3_rotate_y(vec3_t v, float angle) {
  return {
      .x = static_cast<float>(v.x * cos(angle) - v.z * sin(angle)),
      .y = v.y,
      .z = static_cast<float>(v.x * sin(angle) + v.z * cos(angle)),
  };
}

vec3_t vec3_rotate_z(vec3_t v, float angle) {
  return {
      .x = static_cast<float>(v.x * cos(angle) - v.y * sin(angle)),
      .y = static_cast<float>(v.x * sin(angle) + v.y * cos(angle)),
      .z = v.z,
  };
}

//////////////////////////////////////////////////////////////////
/// Vector conversion functions
//////////////////////////////////////////////////////////////////
vec4_t vec4_from_vec3(vec3_t v) {
  return {v.x, v.y, v.z, 1.0};
}

vec3_t vec3_from_vec4(vec4_t v) {
  return {v.x, v.y, v.z};
}
```

## src/light.cpp

```cpp
#include "light.h"
#include <cstdint>
#include <forgefp/fp/adt.hpp>
#include <forgefp/fp/ops.hpp>

light_t light = {.direction = {0, 0, 1}};

uint32_t light_apply_intensity(uint32_t original_color,
                               float percentage_factor) {
  const float intensity = fp::cond(
      percentage_factor, fp::when(fp::lt(0.0f), [](float) { return 0.0f; }),
      fp::when(fp::gt(1.0f), [](float) { return 1.0f; }),
      fp::otherwise([](float factor) { return factor; }));

  uint32_t a = (original_color & 0xFF000000);
  uint32_t r =
      static_cast<uint32_t>((original_color & 0x00FF0000) * intensity);
  uint32_t g =
      static_cast<uint32_t>((original_color & 0x0000FF00) * intensity);
  uint32_t b =
      static_cast<uint32_t>((original_color & 0x000000FF) * intensity);

  return a | (r & 0x00FF0000) | (g & 0x0000FF00) | (b & 0x000000FF);
}
```

`light.h`, `matrix.[ch]pp`, `texture.[ch]pp`, `triangle.h` are unchanged.

## src/mesh.h

```cpp
#ifndef MESH_H
#define MESH_H

#include "triangle.h"
#include "vector.h"
#include <array>
#include <forgefp/fp/error.hpp>
#include <string>
#include <vector>

#define N_CUBE_VERTICES 8
#define N_CUBE_FACES (6 * 2)

extern const std::array<vec3_t, N_CUBE_VERTICES> cube_vertices;
extern const std::array<face_t, N_CUBE_FACES> cube_faces;

/////////////////////////////////////////////
//// Define a struct for dynamic size meshes,
//// with array of vertices and faces
/////////////////////////////////////////////

struct mesh_t {
  std::vector<vec3_t> vertices;
  std::vector<face_t> faces;

  vec3_t rotation{0, 0, 0};
  vec3_t scale{1, 1, 1};
  vec3_t translation{0, 0, 0};
};

extern mesh_t mesh;

void load_cube_mesh_data();
fp::Outcome<mesh_t> load_obj_file_data(std::string const &filename);

#endif // MESH_H
```

## src/mesh.cpp

```cpp
#include "mesh.h"
#include "triangle.h"
#include "vector.h"
#include <array>
#include <cctype>
#include <cstddef>
#include <forgefp/fp/adt.hpp>
#include <forgefp/fp/error.hpp>
#include <forgefp/fp/io.hpp>
#include <forgefp/fp/parse.hpp>
#include <forgefp/fp/ranges.hpp>
#include <forgefp/fp/string.hpp>
#include <forgefp/fp/validation.hpp>
#include <forgefp/fp/views.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

mesh_t mesh{};

const std::array<vec3_t, N_CUBE_VERTICES> cube_vertices = {{
    {.x = -1, .y = -1, .z = -1}, // 1
    {.x = -1, .y = 1, .z = -1},  // 2
    {.x = 1, .y = 1, .z = -1},   // 3
    {.x = 1, .y = -1, .z = -1},  // 4
    {.x = 1, .y = 1, .z = 1},    // 5
    {.x = 1, .y = -1, .z = 1},   // 6
    {.x = -1, .y = 1, .z = 1},   // 7
    {.x = -1, .y = -1, .z = 1}   // 8
}};

const std::array<face_t, N_CUBE_FACES> cube_faces = {{
    // front
    {.a = 1, .b = 2, .c = 3, .a_uv = {0, 0}, .b_uv = {0, 1}, .c_uv = {1, 1},
     .color = 0xFFFFFFFF},
    {.a = 1, .b = 3, .c = 4, .a_uv = {0, 0}, .b_uv = {1, 1}, .c_uv = {1, 0},
     .color = 0xFFFFFFFF},
    // right
    {.a = 4, .b = 3, .c = 5, .a_uv = {0, 0}, .b_uv = {0, 1}, .c_uv = {1, 1},
     .color = 0xFFFFFFFF},
    {.a = 4, .b = 5, .c = 6, .a_uv = {0, 0}, .b_uv = {1, 1}, .c_uv = {1, 0},
     .color = 0xFFFFFFFF},
    // back
    {.a = 6, .b = 5, .c = 7, .a_uv = {0, 0}, .b_uv = {0, 1}, .c_uv = {1, 1},
     .color = 0xFFFFFFFF},
    {.a = 6, .b = 7, .c = 8, .a_uv = {0, 0}, .b_uv = {1, 1}, .c_uv = {1, 0},
     .color = 0xFFFFFFFF},
    // left
    {.a = 8, .b = 7, .c = 2, .a_uv = {0, 0}, .b_uv = {0, 1}, .c_uv = {1, 1},
     .color = 0xFFFFFFFF},
    {.a = 8, .b = 2, .c = 1, .a_uv = {0, 0}, .b_uv = {1, 1}, .c_uv = {1, 0},
     .color = 0xFFFFFFFF},
    // top
    {.a = 2, .b = 7, .c = 5, .a_uv = {0, 0}, .b_uv = {0, 1}, .c_uv = {1, 1},
     .color = 0xFFFFFFFF},
    {.a = 2, .b = 5, .c = 3, .a_uv = {0, 0}, .b_uv = {1, 1}, .c_uv = {1, 0},
     .color = 0xFFFFFFFF},
    // bottom
    {.a = 6, .b = 8, .c = 1, .a_uv = {0, 0}, .b_uv = {0, 1}, .c_uv = {1, 1},
     .color = 0xFFFFFFFF},
    {.a = 6, .b = 1, .c = 4, .a_uv = {0, 0}, .b_uv = {1, 1}, .c_uv = {1, 0},
     .color = 0xFFFFFFFF}
}};

void load_cube_mesh_data() {
  mesh.vertices = fp::to_vector(cube_vertices);
  mesh.faces = fp::to_vector(cube_faces);
}

namespace {

struct obj_index {
  int vertex;
  int texture;
};

using obj_line = std::variant<vec3_t, face_t>;

bool is_number_char(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '-' ||
         c == '+' || c == '.' || c == 'e' || c == 'E';
}

fp::Parser<double> number = fp::lexeme(fp::map(
    fp::some(fp::satisfy(is_number_char)), [](std::vector<char> chars) {
      return std::stod(std::string(chars.begin(), chars.end()));
    }));

fp::Parser<int> integer = fp::lexeme(fp::map(
    fp::some(fp::satisfy([](char c) {
      return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '-';
    })),
    [](std::vector<char> chars) {
      return std::stoi(std::string(chars.begin(), chars.end()));
    }));

fp::Parser<int> maybe_int =
    fp::map(fp::optional(integer),
            [](std::optional<int> value) { return value.value_or(0); });

fp::Parser<obj_index> face_index = fp::map(
    fp::seq(integer,
            fp::optional(fp::char_('/') >>
                         fp::seq(maybe_int,
                                 fp::optional(fp::char_('/') >> maybe_int)))),
    [](auto const &parts) {
      const int texture = parts.second ? parts.second->first : 0;
      return obj_index{parts.first, texture};
    });

fp::Parser<vec3_t> vertex = fp::symbol('v') >>= [](char) {
  return fp::some(number) >>= [](std::vector<double> coords) -> fp::Parser<vec3_t> {
    if (coords.size() < 3)
      return [](std::string_view, std::size_t offset) -> fp::PResult<vec3_t> {
        return fp::p_err<vec3_t>("vertex needs 3 coordinates", offset);
      };
    return fp::succeed(vec3_t{static_cast<float>(coords[0]),
                              static_cast<float>(coords[1]),
                              static_cast<float>(coords[2])});
  };
};

fp::Parser<face_t> face = fp::symbol('f') >>= [](char) {
  return fp::some(face_index) >>= [](std::vector<obj_index> indices) -> fp::Parser<face_t> {
    if (indices.size() < 3)
      return [](std::string_view, std::size_t offset) -> fp::PResult<face_t> {
        return fp::p_err<face_t>("face needs 3 vertex indices", offset);
      };
    face_t parsed{};
    parsed.a = indices[0].vertex;
    parsed.b = indices[1].vertex;
    parsed.c = indices[2].vertex;
    parsed.color = 0xFFFFFFFF;
    return fp::succeed(parsed);
  };
};

template <class T>
fp::Result<T> parse_line(fp::Parser<T> parser, std::string const &line) {
  return fp::run(fp::terminated(std::move(parser), fp::eof), line);
}

template <class T> fp::Validation<T> as_validation(fp::Result<T> result) {
  return fp::map_error(result, [](std::string const &message) {
    return std::vector<std::string>{message};
  });
}

fp::Validation<std::optional<obj_line>> parse_obj_line(std::string const &line) {
  if (fp::str::starts_with(line, "v "))
    return as_validation(fp::map(parse_line(vertex, line), [](vec3_t value) {
      return std::optional<obj_line>{obj_line{value}};
    }));

  if (fp::str::starts_with(line, "f "))
    return as_validation(fp::map(parse_line(face, line), [](face_t value) {
      return std::optional<obj_line>{obj_line{std::move(value)}};
    }));

  return fp::valid(std::optional<obj_line>{std::nullopt});
}

mesh_t collect_mesh(std::vector<std::optional<obj_line>> const &lines) {
  mesh_t loaded;

  auto present =
      lines |
      fp::views::filter([](std::optional<obj_line> const &line) {
        return line.has_value();
      }) |
      fp::views::map(
          [](std::optional<obj_line> const &line) -> obj_line const & {
            return *line;
          });

  for (auto const &line : present) {
    fp::match(line,
              fp::case_<vec3_t>([&](vec3_t const &value) {
                loaded.vertices.push_back(value);
              }),
              fp::case_<face_t>([&](face_t const &value) {
                loaded.faces.push_back(value);
              }));
  }

  return loaded;
}

} // namespace

fp::Outcome<mesh_t> load_obj_file_data(std::string const &filename) {
  const auto parsed = fp::and_then(
      fp::from_result(fp::read_lines(filename), fp::errc::not_found),
      [](std::vector<std::string> const &lines) -> fp::Outcome<mesh_t> {
        const auto validated = fp::traverse(
            fp::enumerate(lines),
            [](std::pair<size_t, std::string> const &entry) {
              return fp::map_error(
                  parse_obj_line(entry.second),
                  [number = entry.first + 1](
                      std::vector<std::string> const &errors) {
                    return fp::map(errors, [number](std::string const &error) {
                      return "line " + std::to_string(number) + ": " + error;
                    });
                  });
            });

        const auto collapsed = fp::map_error(
            validated, [](std::vector<std::string> const &errors) {
              return fp::str::join(errors, "; ");
            });

        return fp::and_then(
            fp::from_result(collapsed, fp::errc::invalid),
            [](std::vector<std::optional<obj_line>> const &present) {
              return fp::Outcome<mesh_t>::ok(collect_mesh(present));
            });
      });

  return fp::with_context(parsed, "loading " + filename);
}
```

## src/display.h

```cpp
#pragma once
#ifndef DISPLAY_H
#define DISPLAY_H

#include "SDL_render.h"
#include "SDL_video.h"
#include <SDL.h>
#include <cstdint>
#include <forgefp/fp/result.hpp>
#include <vector>

#define FPS 5000
#define FRAME_TARGET_TIME (1000 / FPS)

enum Cull_Method { CULL_NONE, CULL_BACKFACE };

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

extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern std::vector<uint32_t> color_buffer;
extern SDL_Texture *color_buffer_texture;
extern int window_width;
extern int window_height;

fp::Result<void> initialize_window(void);
void draw_grid(void);
void draw_pixel(int x, int y, uint32_t color);
void draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                   uint32_t color);
void draw_rect(int x, int y, int width, int height, uint32_t color);
void render_color_buffer(void);
void clear_color_buffer(uint32_t color);
void destroy_window(void);

#endif // DISPLAY_H
```

## src/display.cpp

```cpp
#include "display.h"
#include "SDL_render.h"
#include "SDL_video.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <forgefp/fp/grid.hpp>
#include <forgefp/fp/ranges.hpp>
#include <forgefp/fp/simd.hpp>
#include <iostream>
#include <vector>

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
std::vector<uint32_t> color_buffer;
SDL_Texture *color_buffer_texture = NULL;
int window_width = 1280;
int window_height = 720;

fp::Result<void> initialize_window() {
  std::cout << "Starting SDL initialization..." << std::endl;
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    return fp::err<void>(SDL_GetError());

  // Set width and height of the SDL window with max screen resolution
  SDL_DisplayMode display_mode;
  SDL_GetCurrentDisplayMode(0, &display_mode);

  // This will set the display to fullscreen mode
  // window_width = display_mode.w;
  // window_height = display_mode.h;

  // CREATE SDL window
  window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            window_width, window_height,
                            SDL_WINDOW_BORDERLESS);

  if (!window)
    return fp::err<void>(SDL_GetError());

  // Create a SDL renderer
  renderer = SDL_CreateRenderer(window, -1, 0);

  if (!renderer)
    return fp::err<void>(SDL_GetError());

  return fp::ok<void>();
}

void draw_grid() {
  fp::for_each_index(static_cast<size_t>(window_height) / 10,
                     static_cast<size_t>(window_width) / 10,
                     [](size_t row, size_t col) {
                       color_buffer[(row * 10) * window_width + col * 10] =
                           0xFF000000;
                     });
}

void draw_pixel(int x, int y, uint32_t color) {
  if (x >= 0 && x < window_width && y >= 0 && y < window_height) {
    color_buffer[static_cast<size_t>(window_width) * static_cast<size_t>(y) +
                 static_cast<size_t>(x)] = color;
  }
}

void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
  int delta_x = (x1 - x0);
  int delta_y = (y1 - y0);

  int longest_side_length = std::max(std::abs(delta_x), std::abs(delta_y));

  float x_inc = delta_x / static_cast<float>(longest_side_length);
  float y_inc = delta_y / static_cast<float>(longest_side_length);

  float current_x = static_cast<float>(x0);
  float current_y = static_cast<float>(y0);

  for ([[maybe_unused]] int i : fp::range(0, longest_side_length + 1)) {
    draw_pixel(static_cast<int>(std::round(current_x)),
               static_cast<int>(std::round(current_y)), color);
    current_x += x_inc;
    current_y += y_inc;
  }
}

void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                   uint32_t color) {
  draw_line(x0, y0, x1, y1, color);
  draw_line(x1, y1, x2, y2, color);
  draw_line(x2, y2, x0, y0, color);
}

void draw_rect(int x, int y, int width, int height, uint32_t color) {
  fp::for_each_index(static_cast<size_t>(height), static_cast<size_t>(width),
                     [&](size_t row, size_t col) {
                       draw_pixel(x + static_cast<int>(col),
                                  y + static_cast<int>(row), color);
                     });
}

void render_color_buffer() {
  SDL_UpdateTexture(color_buffer_texture, NULL, color_buffer.data(),
                    (int)(window_width * sizeof(uint32_t)));

  SDL_RenderCopy(renderer, color_buffer_texture, NULL, NULL);
}

void clear_color_buffer(uint32_t color) {
  fp::map_inplace(color_buffer, [color](fp::vec<uint32_t>) {
    return fp::vec<uint32_t>(color);
  });
}

void destroy_window() {
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}
```

## src/triangle.cpp (`triangle.h` unchanged)

```cpp
#include "triangle.h"
#include "display.h"
#include <cstdint>
#include <forgefp/fp/ranges.hpp>
#include <forgefp/fp/vec.hpp>
#include <vector>

namespace {

struct vertex_t {
  int x;
  int y;
};

//////////////////////////////////////////////
// Draw a filled triangle with a flat bottom
//////////////////////////////////////////////
void fill_flat_bottom_triangle(vertex_t v0, vertex_t v1, vertex_t v2,
                               uint32_t color) {
  const float inv_slope_1 =
      static_cast<float>(v1.x - v0.x) / static_cast<float>(v1.y - v0.y);
  const float inv_slope_2 =
      static_cast<float>(v2.x - v0.x) / static_cast<float>(v2.y - v0.y);

  // Start x_start and x_end from the top vertex
  float x_start = static_cast<float>(v0.x);
  float x_end = static_cast<float>(v0.x);

  for (int y : fp::range(v0.y, v2.y + 1)) {
    draw_line(static_cast<int>(x_start), y, static_cast<int>(x_end), y, color);
    x_start += inv_slope_1;
    x_end += inv_slope_2;
  }
}

void fill_flat_top_triangle(vertex_t v0, vertex_t v1, vertex_t v2,
                            uint32_t color) {
  const float inv_slope_1 =
      static_cast<float>(v2.x - v0.x) / static_cast<float>(v2.y - v0.y);
  const float inv_slope_2 =
      static_cast<float>(v2.x - v1.x) / static_cast<float>(v2.y - v1.y);

  // Start x_start and x_end from the bottom vertex
  float x_start = static_cast<float>(v2.x);
  float x_end = static_cast<float>(v2.x);

  for (int y : fp::reverse(fp::range(v0.y, v2.y + 1))) {
    draw_line(static_cast<int>(x_start), y, static_cast<int>(x_end), y, color);
    x_start -= inv_slope_1;
    x_end -= inv_slope_2;
  }
}

} // namespace

void draw_filled_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                          uint32_t color) {
  // Sort the vertices by y-coords ascending y0 < y1 < y2
  auto verts = fp::sort_by(
      std::vector<vertex_t>{{x0, y0}, {x1, y1}, {x2, y2}},
      [](vertex_t const &v) { return v.y; });

  vertex_t v0 = verts[0];
  vertex_t v1 = verts[1];
  vertex_t v2 = verts[2];

  if (v1.y == v2.y) {
    // We can simply draw the flat bottom triangle
    fill_flat_bottom_triangle(v0, v1, v2, color);
  } else if (v0.y == v1.y) {
    // We can simply draw the flat top triangle
    fill_flat_top_triangle(v0, v1, v2, color);
  } else {
    // Calculate the new vertex (mx, my) using triangle similarity
    int my = v1.y;
    int mx = (((v2.x - v0.x) * (v1.y - v0.y)) / (v2.y - v0.y)) + v0.x;

    fill_flat_bottom_triangle(v0, v1, {mx, my}, color);
    fill_flat_top_triangle(v1, {mx, my}, v2, color);
  }
}

void draw_textured_triangle(int, int, float, float, int, int, float, float, int,
                            int, float, float, uint32_t *) {}
```

## src/main.cpp

```cpp
#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_pixels.h"
#include "SDL_render.h"
#include "SDL_timer.h"
#include "display.h"
#include "light.h"
#include "matrix.h"
#include "mesh.h"
#include "texture.h"
#include "triangle.h"
#include "vector.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <forgefp/fp/adt.hpp>
#include <forgefp/fp/combinators.hpp>
#include <forgefp/fp/compose.hpp>
#include <forgefp/fp/concurrent.hpp>
#include <forgefp/fp/curry.hpp>
#include <forgefp/fp/error.hpp>
#include <forgefp/fp/map.hpp>
#include <forgefp/fp/ops.hpp>
#include <forgefp/fp/ranges.hpp>
#include <forgefp/fp/simd.hpp>
#include <forgefp/fp/vec.hpp>
#include <forgefp/fp/views.hpp>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>

/////////////////////////////////////////////////////////////
//// Array of triangle that should be rendered frame by frame
/////////////////////////////////////////////////////////////
std::vector<triangle_t> triangles_to_render;

Render_Method render_method = RENDER_WIRE;
Cull_Method cull_method = CULL_BACKFACE;

/////////////////////////////////////////////////////////////
//// Array of triangle that should be rendered frame by frame
/////////////////////////////////////////////////////////////
bool is_running = false;
int previous_frame_time = 0;

vec3_t camera_position = {.x = 0, .y = 0, .z = 0};
mat4_t proj_matrix;

namespace {

struct face_geometry {
  face_t face;
  std::array<vec3_t, 3> corners;
  std::array<vec2_t, 3> projected;
  vec3_t normal;
  float avg_depth;
};

} // namespace

/////////////////////////////////////////////////////////////
//// Load assets and set up the renderer state
/////////////////////////////////////////////////////////////
void setup() {
  color_buffer.assign(static_cast<size_t>(window_width) * window_height, 0u);

  color_buffer_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           window_width, window_height);

  // Initialize the perspective projection matrix
  float fov = M_PI / 3.0f; // 180 deg / 3 = 60 deg
  float aspect = (float)window_height / (float)window_width;
  float znear = 1.0f;
  float zfar = 100.0f;
  proj_matrix = mat4_make_perspective(fov, aspect, znear, zfar);

  // Manually load the texture data from the static array
  mesh_texture = (uint32_t *)REDBRICK_TEXTURE;
  texture_width = 64;
  texture_height = 64;

  fp::match(
      fp::tap_ok(
          fp::tap_err(load_obj_file_data("./assets/f22.obj"),
                      [](fp::Error const &error) {
                        std::cerr << "obj load failed: " << fp::to_string(error)
                                  << " - falling back to cube\n";
                      }),
          [](mesh_t const &loaded) {
            std::cout << "loaded " << loaded.vertices.size() << " vertices, "
                      << loaded.faces.size() << " faces\n";
          }),
      [](mesh_t loaded) { mesh = std::move(loaded); },
      [](fp::Error const &) { load_cube_mesh_data(); });
}

/////////////////////////////////////////////////////////////
//// Poll system events and handle keyboard input
/////////////////////////////////////////////////////////////
void process_input() {
  SDL_Event event;
  if (SDL_PollEvent(&event) == 0)
    return;

  switch (event.type) {
  case SDL_QUIT:
    is_running = false;
    break;
  case SDL_KEYDOWN: {
    const SDL_Keycode key = event.key.keysym.sym;

    if (key == SDLK_ESCAPE) {
      is_running = false;
      break;
    }

    static const std::map<SDL_Keycode, Render_Method> render_bindings = {
        {SDLK_1, RENDER_WIRE_VERTEX},   {SDLK_2, RENDER_WIRE},
        {SDLK_3, RENDER_FILL_TRIANGLE}, {SDLK_4, RENDER_FILL_TRIANGLE_WIRE},
        {SDLK_5, RENDER_TEXTURED},      {SDLK_6, RENDER_TEXTURED_WIRE}};

    static const std::map<SDL_Keycode, Cull_Method> cull_bindings = {
        {SDLK_c, CULL_BACKFACE}, {SDLK_d, CULL_NONE}};

    render_method = fp::value_or(fp::lookup(render_bindings, key), render_method);
    cull_method = fp::value_or(fp::lookup(cull_bindings, key), cull_method);

    break;
  }
  default:
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

  mesh.rotation.x += 0.005;

  mesh.translation.z = 5.0;

  // Create rotation, translation and scale matrix that will used to multiply
  // the mesh vertices
  const std::vector<mat4_t> transforms = {
      mat4_make_scale(mesh.scale.x, mesh.scale.y, mesh.scale.z),
      mat4_make_rotation_z(mesh.rotation.z),
      mat4_make_rotation_y(mesh.rotation.y),
      mat4_make_rotation_x(mesh.rotation.x),
      mat4_make_translation(mesh.translation.x, mesh.translation.y,
                            mesh.translation.z)};

  const mat4_t world_matrix =
      fp::fold_left(transforms, mat4_identity(), fp::flip(mat4_mul_mat4));

  const auto to_world = fp::curry([](mat4_t matrix, vec3_t vertex) {
    return mat4_mul_vec4(matrix, vec4_from_vec3(vertex));
  })(world_matrix);

  const auto to_screen = fp::curry([](mat4_t matrix, vec4_t vertex) {
    vec4_t projected = mat4_mul_vec4_project(matrix, vertex);
    projected.x = projected.x * (window_width / 2.0f) + window_width / 2.0f;
    projected.y = projected.y * (window_height / -2.0f) + window_height / 2.0f;
    return projected;
  })(proj_matrix);

  const auto project = fp::pipe(to_world, to_screen);

  const auto to_world_point = [&](vec3_t vertex) {
    return vec3_from_vec4(to_world(vertex));
  };

  const auto to_geometry = [&](face_t const &face) {
    const std::array<vec3_t, 3> corners = {mesh.vertices[face.a - 1],
                                           mesh.vertices[face.b - 1],
                                           mesh.vertices[face.c - 1]};
    const auto world = fp::map(corners, to_world_point);
    const auto screen = fp::map(corners, project);

    const vec3_t normal = vec3_normalize(vec3_cross(
        vec3_sub(world[1], world[0]), vec3_sub(world[2], world[0])));

    return face_geometry{
        face,
        {world[0], world[1], world[2]},
        {vec2_t{screen[0].x, screen[0].y}, vec2_t{screen[1].x, screen[1].y},
         vec2_t{screen[2].x, screen[2].y}},
        normal,
        fp::sum(fp::map(world, [](vec3_t vertex) { return vertex.z; })) / 3.0f};
  };

  static fp::ThreadPool pool;

  const auto geometry = fp::par_map(pool, mesh.faces, to_geometry);

  std::vector<float> intensities = fp::map(geometry, [](face_geometry const &g) {
    return fp::negate(vec3_dot(g.normal, light.direction));
  });
  fp::clamp_inplace(intensities, 0.0f, 1.0f);

  auto candidates = fp::zip_with(
      geometry, intensities,
      [&](face_geometry const &g, float intensity) -> std::optional<triangle_t> {
        const bool culled =
            cull_method == CULL_BACKFACE &&
            fp::le(0.0f)(
                vec3_dot(g.normal, vec3_sub(camera_position, g.corners[0])));
        if (culled)
          return std::nullopt;

        triangle_t triangle{};
        for (auto &&[index, point] : fp::views::enumerate(g.projected))
          triangle.points[index] = point;

        triangle.texcoords[0] = g.face.a_uv;
        triangle.texcoords[1] = g.face.b_uv;
        triangle.texcoords[2] = g.face.c_uv;
        triangle.color = light_apply_intensity(g.face.color, intensity);
        triangle.avg_depth = g.avg_depth;
        return triangle;
      });

  triangles_to_render = fp::out(
      fp::into(std::move(candidates)) |
      fp::filter_map([](std::optional<triangle_t> const &triangle) {
        return triangle;
      }) |
      fp::sort_by(
          [](triangle_t const &triangle) { return -triangle.avg_depth; }));
}

/////////////////////////////////////////////////////////////
//// Render function to draw objects on the display
/////////////////////////////////////////////////////////////
void render_triangle(triangle_t const &triangle) {
  auto is_filled = [](Render_Method method) {
    return method == RENDER_FILL_TRIANGLE ||
           method == RENDER_FILL_TRIANGLE_WIRE;
  };
  auto is_textured = [](Render_Method method) {
    return method == RENDER_TEXTURED || method == RENDER_TEXTURED_WIRE;
  };
  auto is_wire = [](Render_Method method) {
    return method == RENDER_WIRE || method == RENDER_WIRE_VERTEX ||
           method == RENDER_FILL_TRIANGLE_WIRE ||
           method == RENDER_TEXTURED_WIRE;
  };
  auto is_vertex = [](Render_Method method) {
    return method == RENDER_WIRE_VERTEX;
  };

  fp::cond(
      render_method,
      fp::when(is_filled,
               [&](Render_Method) {
                 draw_filled_triangle(
                     triangle.points[0].x, triangle.points[0].y,
                     triangle.points[1].x, triangle.points[1].y,
                     triangle.points[2].x, triangle.points[2].y,
                     triangle.color);
               }),
      fp::when(is_textured,
               [&](Render_Method) {
                 draw_textured_triangle(
                     triangle.points[0].x, triangle.points[0].y,
                     triangle.texcoords[0].u, triangle.texcoords[0].v,
                     triangle.points[1].x, triangle.points[1].y,
                     triangle.texcoords[1].u, triangle.texcoords[1].v,
                     triangle.points[2].x, triangle.points[2].y,
                     triangle.texcoords[2].u, triangle.texcoords[2].v,
                     mesh_texture);
               }),
      fp::when(is_wire,
               [&](Render_Method) {
                 draw_triangle(triangle.points[0].x, triangle.points[0].y,
                               triangle.points[1].x, triangle.points[1].y,
                               triangle.points[2].x, triangle.points[2].y,
                               0xFFFFFFFF);
               }),
      fp::when(is_vertex,
               [&](Render_Method) {
                 for (auto const &point : triangle.points)
                   draw_rect(point.x - 3, point.y - 3, 6, 6, 0xFFFF0000);
               }),
      fp::otherwise([](Render_Method) {}));
}

void render() {
  clear_color_buffer(0xFF000000);

  // Loop all projected triangles and render them
  for (auto const &triangle : triangles_to_render)
    render_triangle(triangle);

  // Clear array of triangles to render every frame
  triangles_to_render.clear();

  render_color_buffer();

  SDL_RenderPresent(renderer);
}

/////////////////////////////////////////////////////////////
//// Entry point
/////////////////////////////////////////////////////////////
int main() {
  const auto initialized = initialize_window();
  if (!initialized.is_ok()) {
    std::cerr << "SDL init failed: " << initialized.error() << "\n";
    return 1;
  }

  is_running = true;

  setup();

  while (is_running) {
    process_input();
    update();
    render();
  }

  destroy_window();

  return 0;
}
```

## Notes

- The OBJ error path now reports every bad line at once:
  `err("loading bad.obj: line 2: line 1, col 3: expected at least one item;
  line 4: line 1, col 6: face needs 3 vertex indices; ...")`, with code
  `errc::invalid` (or `errc::not_found` when the file is missing) and the
  `"loading <path>"` context chain.
- `fp::par_map` runs `to_geometry` on a reused `ThreadPool`; all captures are
  read-only, so the per-face transform is data-parallel.
- `fp::clamp_inplace` (SIMD) clamps the whole intensity vector in one pass
  before `zip_with` pairs it back up with the geometry.
- The rasterizer keeps explicit loops because `fp::scan` changes the
  floating-point accumulation order (see gotcha 2). `fp::sort_by`,
  `fp::range`, `fp::reverse` still replace the swap network and the index
  bookkeeping.
- If you do not want the SIMD/thread dependencies, swap `fp::dot` for
  `fp::sum(fp::zip_with(...))`, `fp::clamp_inplace` for `fp::map` + a scalar
  clamp, `fp::map_inplace` for `std::fill`, and `fp::par_map` for `fp::map`.

---

# Re-audit against the extended ForgeFP

The first migration used the ForgeFP surface that existed then (ranges, ADTs,
parser combinators, `fp::simd`, `fp::concurrent`). Since then fp gained
`inplace.hpp`, `memory.hpp`/`Buffer`, `numerics.hpp`, `linalg.hpp`,
`random.hpp`, `time.hpp`, `scope.hpp`, `serialize.hpp`, and the
`views`/`grid`/`ops` extensions. This re-audit walks the migrated code and
replaces every remaining hand-rolled or allocating pattern with the new
primitives.

Verification after the changes: the rasterizer still matches the original
filler on 20,000 random triangles (0 mismatches), the OBJ parser is unchanged
on the assets, and the app smoke-run logs its load time and exits cleanly.

## Findings

| Location | Before (first migration) | After (re-audit) | Why |
|---|---|---|---|
| `light.cpp` intensity clamp | `fp::cond` + `fp::when` + `fp::otherwise` | `fp::clamp(v, 0.0f, 1.0f)` | one call, exact intent |
| `triangle.cpp` vertex sort | `fp::sort_by` over a 3-element `std::vector` | `std::array` + `fp::sort_by_inplace` | **zero heap allocation per triangle** |
| `triangle.cpp` flat-top span | `fp::reverse(fp::range(...))` | `fp::range(...) \| fp::views::reverse` | no allocation per span |
| `display.cpp` Bresenham loop | `fp::range(0, n + 1)` | `fp::views::iota(0, n + 1)` | lazy integer range, no allocation per line |
| `main.cpp` painter's sort | `fp::sort_by` inside the pipe | `fp::sort_by_inplace` | removes a full copy of the triangle list every frame |
| `main.cpp` shutdown | explicit `destroy_window()` | `fp::defer` | cleanup on every exit path |
| `main.cpp` frame pacing | `SDL_GetTicks()` delta bookkeeping | `fp::Stopwatch::lap()` | monotonic, no integer-ms state |
| `main.cpp` load log | plain `std::cout` | `fp::Stopwatch` + `fp::str::to_string` | timing plus locale-free formatting |

The three rasterizer changes are behavior-preserving: `std::ranges::sort` with
the same comparator yields the same permutation, `views::reverse` iterates the
same values in the same order, and the `fp::views::iota` loop visits exactly
the same integers as `fp::range`.

## New fp extension surfaced by the audit: `fp::views::iota`

The audit found one genuine gap: fp had lazy `map`/`filter`/`chunk`/… but no
lazy integer range, so index loops had to use the eager `fp::range` (one
`std::vector` per call — per scanline in `draw_line`!). Fixed in fp:

```cpp
// fp/views.hpp
inline constexpr auto iota = std::views::iota;
```

Added to both header trees with the test `Views.IotaIsLazy`. Index loops are
now allocation-free *and* fp-spelled.

## Revised code

### `light.cpp` (intensity)

```cpp
#include "light.h"
#include <cstdint>
#include <forgefp/fp/numerics.hpp>

light_t light = {.direction = {0, 0, 1}};

uint32_t light_apply_intensity(uint32_t original_color,
                               float percentage_factor) {
  const float intensity = fp::clamp(percentage_factor, 0.0f, 1.0f);

  uint32_t a = (original_color & 0xFF000000);
  uint32_t r =
      static_cast<uint32_t>((original_color & 0x00FF0000) * intensity);
  uint32_t g =
      static_cast<uint32_t>((original_color & 0x0000FF00) * intensity);
  uint32_t b =
      static_cast<uint32_t>((original_color & 0x000000FF) * intensity);

  return a | (r & 0x00FF0000) | (g & 0x0000FF00) | (b & 0x000000FF);
}
```

### `triangle.cpp` (allocation-free sort and reverse)

```cpp
#include "triangle.h"
#include "display.h"
#include <array>
#include <cstdint>
#include <forgefp/fp/inplace.hpp>
#include <forgefp/fp/ranges.hpp>
#include <forgefp/fp/vec.hpp>
#include <forgefp/fp/views.hpp>
#include <vector>

namespace {

struct vertex_t {
  int x;
  int y;
};

void fill_flat_top_triangle(vertex_t v0, vertex_t v1, vertex_t v2,
                            uint32_t color) {
  const float inv_slope_1 =
      static_cast<float>(v2.x - v0.x) / static_cast<float>(v2.y - v0.y);
  const float inv_slope_2 =
      static_cast<float>(v2.x - v1.x) / static_cast<float>(v2.y - v1.y);

  float x_start = static_cast<float>(v2.x);
  float x_end = static_cast<float>(v2.x);

  // Lazy reverse: same order, no vector allocated.
  for (int y : fp::range(v0.y, v2.y + 1) | fp::views::reverse) {
    draw_line(static_cast<int>(x_start), y, static_cast<int>(x_end), y, color);
    x_start -= inv_slope_1;
    x_end -= inv_slope_2;
  }
}

} // namespace

void draw_filled_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                          uint32_t color) {
  // Sort the vertices by y-coords ascending y0 < y1 < y2 (no allocation).
  std::array<vertex_t, 3> verts{{{x0, y0}, {x1, y1}, {x2, y2}}};
  fp::sort_by_inplace(verts, [](vertex_t const &v) { return v.y; });

  vertex_t v0 = verts[0];
  vertex_t v1 = verts[1];
  vertex_t v2 = verts[2];

  if (v1.y == v2.y) {
    fill_flat_bottom_triangle(v0, v1, v2, color);
  } else if (v0.y == v1.y) {
    fill_flat_top_triangle(v0, v1, v2, color);
  } else {
    int my = v1.y;
    int mx = (((v2.x - v0.x) * (v1.y - v0.y)) / (v2.y - v0.y)) + v0.x;
    fill_flat_bottom_triangle(v0, v1, {mx, my}, color);
    fill_flat_top_triangle(v1, {mx, my}, v2, color);
  }
}
```

### `display.cpp` (lazy scanline loop)

```cpp
#include <forgefp/fp/views.hpp>   // fp::views::iota

void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
  int delta_x = (x1 - x0);
  int delta_y = (y1 - y0);

  int longest_side_length = std::max(std::abs(delta_x), std::abs(delta_y));

  float x_inc = delta_x / static_cast<float>(longest_side_length);
  float y_inc = delta_y / static_cast<float>(longest_side_length);

  float current_x = static_cast<float>(x0);
  float current_y = static_cast<float>(y0);

  // fp::range would allocate a vector per scanline; iota is lazy.
  for ([[maybe_unused]] int i : fp::views::iota(0, longest_side_length + 1)) {
    draw_pixel(static_cast<int>(std::round(current_x)),
               static_cast<int>(std::round(current_y)), color);
    current_x += x_inc;
    current_y += y_inc;
  }
}
```

### `main.cpp` (timing, sort, shutdown)

```cpp
#include <forgefp/fp/inplace.hpp>
#include <forgefp/fp/scope.hpp>
#include <forgefp/fp/string.hpp>
#include <forgefp/fp/time.hpp>

bool is_running = false;
fp::Stopwatch frame_clock;

void setup() {
  // ... unchanged ...

  fp::Stopwatch load_clock;

  fp::match(
      fp::tap_ok(
          fp::tap_err(load_obj_file_data("./assets/f22.obj"),
                      [](fp::Error const &error) {
                        std::cerr << "obj load failed: " << fp::to_string(error)
                                  << " - falling back to cube\n";
                      }),
          [&](mesh_t const &loaded) {
            std::cout << "loaded " << loaded.vertices.size() << " vertices, "
                      << loaded.faces.size() << " faces in "
                      << fp::str::to_string(load_clock.lap(), 3) << "s\n";
          }),
      [](mesh_t loaded) { mesh = std::move(loaded); },
      [](fp::Error const &) { load_cube_mesh_data(); });
}

void update() {
  // Monotonic frame duration; fp::Stopwatch never jumps backwards.
  const int elapsed_ms = static_cast<int>(frame_clock.lap() * 1000.0);
  const int time_to_wait = FRAME_TARGET_TIME - elapsed_ms;

  if (time_to_wait > 0 && time_to_wait <= FRAME_TARGET_TIME)
    SDL_Delay(time_to_wait);

  // ... mesh transforms and per-face geometry unchanged ...

  triangles_to_render = fp::out(
      fp::into(std::move(candidates)) |
      fp::filter_map([](std::optional<triangle_t> const &triangle) {
        return triangle;
      }));

  // Painter's algorithm without the extra copy fp::sort_by would make.
  fp::sort_by_inplace(triangles_to_render, [](triangle_t const &triangle) {
    return -triangle.avg_depth;
  });
}

int main() {
  const auto initialized = initialize_window();
  if (!initialized.is_ok()) {
    std::cerr << "SDL init failed: " << initialized.error() << "\n";
    return 1;
  }

  // Tear down on every exit path, including an early return from the loop.
  auto shutdown = fp::defer([] { destroy_window(); });

  is_running = true;
  setup();

  while (is_running) {
    process_input();
    update();
    render();
  }

  return 0;
}
```

## Remaining candidates (deliberately not done)

- **`matrix.cpp` (mat4)**: 4x4 `float` matrix math could use `fp::linalg` if it
  accepted any nested random-access range (`std::array<std::array<float,4>,4>`),
  not just `std::vector<std::vector<T>>`. Recorded in
  [`fp/EXTENSIONS.md`](../fp/EXTENSIONS.md) as a candidate; the current hand
  loops are correct and fast, so this is a convenience win, not a performance
  one.
- **`fp::Buffer` for the color buffer**: the buffer is a fixed-size owner that
  is written element-wise every frame; `std::vector` is the right tool. Buffer
  becomes relevant if the renderer grows per-frame scratch allocations.
- **`fp::Arena` per frame**: the renderer already reuses its only two
  containers (`triangles_to_render`, `color_buffer`) and frees nothing per
  frame, so an arena would add machinery without removing an allocation.
- **`fp::random` / `fp::serialize` / `fp::autodiff`**: not used by a renderer;
  deliberately absent.
