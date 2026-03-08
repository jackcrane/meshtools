#include "meshtools/platform/FileDialog.h"

#import <Cocoa/Cocoa.h>

#include <string>

namespace meshtools::platform {

std::optional<std::filesystem::path> openMeshFileDialog() {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setAllowedFileTypes:@[ @"obj", @"stl" ]];
        [panel setTitle:@"Open Mesh"];
        [panel setPrompt:@"Open"];

        const NSInteger result = [panel runModal];
        if (result != NSModalResponseOK || panel.URL == nil) {
            return std::nullopt;
        }

        NSString* selected_path = panel.URL.path;
        if (selected_path == nil) {
            return std::nullopt;
        }

        return std::filesystem::path(std::string([selected_path UTF8String]));
    }
}

}  // namespace meshtools::platform
