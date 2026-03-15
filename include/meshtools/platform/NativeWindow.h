#pragma once

struct GLFWwindow;

namespace meshtools::platform {

void syncNativeWindowTheme(GLFWwindow* window, float red, float green, float blue, float alpha);

}  // namespace meshtools::platform
