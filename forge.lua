-- Forge Project Configuration
-- This file replaces package.toml

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
    },
  },
  scripts = {
    -- ["pre-build"] = "doxygen",
  },
}
