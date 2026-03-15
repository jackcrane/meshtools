#include "meshtools/platform/NativeWindow.h"

#import <Cocoa/Cocoa.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace meshtools::platform {

void syncNativeWindowTheme(GLFWwindow* window, float red, float green, float blue, float alpha) {
    if (window == nullptr) {
        return;
    }

    @autoreleasepool {
        NSWindow* cocoa_window = glfwGetCocoaWindow(window);
        if (cocoa_window == nil) {
            return;
        }

        const float luminance = (red * 0.2126F) + (green * 0.7152F) + (blue * 0.0722F);
        NSColor* background_color = [NSColor colorWithSRGBRed:red green:green blue:blue alpha:alpha];
        NSAppearanceName appearance_name = luminance < 0.5F ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
        NSAppearance* appearance = [NSAppearance appearanceNamed:appearance_name];
        [cocoa_window setBackgroundColor:background_color];
        [cocoa_window setTitlebarAppearsTransparent:YES];
        [cocoa_window setOpaque:(alpha >= 1.0F)];
        [cocoa_window setAppearance:appearance];
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 110000
        if ([cocoa_window respondsToSelector:@selector(setToolbarStyle:)]) {
            [cocoa_window setToolbarStyle:(luminance < 0.5F ? NSWindowToolbarStyleUnifiedCompact : NSWindowToolbarStyleUnified)];
        }
#endif
        if ([cocoa_window.contentView respondsToSelector:@selector(setAppearance:)]) {
            [cocoa_window.contentView setAppearance:appearance];
        }
    }
}

}  // namespace meshtools::platform
