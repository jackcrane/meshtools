# MeshTools

This repository now contains a small but extensible C++ application scaffold for a future mesh editor. The current target is intentionally narrow: open a native window, initialize Dear ImGui, and render a dockable editor shell.

## Tech stack

- CMake 3.31+
- C++20
- GLFW for windowing and input
- OpenGL for rendering
- Dear ImGui for UI

## Project layout

- `src/app`: application lifecycle and main loop
- `src/platform`: native window and OpenGL context setup
- `src/ui`: Dear ImGui initialization and editor panels
- `include/meshtools`: public headers for the internal modules
- `cmake`: reusable CMake helpers

## Build

The default configuration downloads `GLFW` and `Dear ImGui` at CMake configure time with `FetchContent`.

```bash
cmake --preset debug --fresh
cmake --build --preset debug
./build/debug/meshtools
```

If you want to use local source checkouts instead of network fetches:

```bash
cmake --preset debug --fresh \
  -DMESHTOOLS_USE_FETCHCONTENT=OFF \
  -DMESHTOOLS_GLFW_SOURCE_DIR=/path/to/glfw \
  -DMESHTOOLS_IMGUI_SOURCE_DIR=/path/to/imgui
```

Notes:

- `Dear ImGui` should come from the `docking` branch if you want the dockspace UI in this scaffold.
- The first successful configure needs either network access for `FetchContent` or local source directories for both dependencies.

## Current behavior

- Opens a resizable window
- Creates an OpenGL context
- Boots Dear ImGui with docking and multi-viewport enabled
- Shows a dockspace, inspector panel, stats panel, and the ImGui demo window

## Next steps

Natural follow-ons for the mesh editor:

- renderer abstraction and scene view
- command system and undo/redo
- document model for meshes, selections, and tools
- serialization and asset import/export
