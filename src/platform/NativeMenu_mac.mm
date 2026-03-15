#include "meshtools/platform/NativeMenu.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using meshtools::platform::NativeMenuActions;

static NativeMenuActions g_pending_actions;

@interface MeshToolsSampleMenuItem : NSMenuItem
@property(nonatomic, copy) NSString* samplePath;
@end

@implementation MeshToolsSampleMenuItem
@end

@interface MeshToolsMenuTarget : NSObject
- (void)openDocument:(id)sender;
- (void)openSample:(id)sender;
- (void)saveProject:(id)sender;
- (void)saveProjectAs:(id)sender;
- (void)undoEdit:(id)sender;
- (void)redoEdit:(id)sender;
- (void)openSettings:(id)sender;
- (void)quitApplication:(id)sender;
@end

@implementation MeshToolsMenuTarget
- (void)openDocument:(id)sender {
    (void)sender;
    g_pending_actions.open_document = true;
}

- (void)openSample:(id)sender {
    MeshToolsSampleMenuItem* item = (MeshToolsSampleMenuItem*)sender;
    if (item.samplePath == nil) {
        return;
    }

    g_pending_actions.open_sample_path = std::filesystem::path(std::string([item.samplePath UTF8String]));
}

- (void)saveProject:(id)sender {
    (void)sender;
    g_pending_actions.save_project = true;
}

- (void)saveProjectAs:(id)sender {
    (void)sender;
    g_pending_actions.save_project_as = true;
}

- (void)undoEdit:(id)sender {
    (void)sender;
    g_pending_actions.undo = true;
}

- (void)redoEdit:(id)sender {
    (void)sender;
    g_pending_actions.redo = true;
}

- (void)openSettings:(id)sender {
    (void)sender;
    g_pending_actions.open_settings = true;
}

- (void)quitApplication:(id)sender {
    (void)sender;
    g_pending_actions.quit = true;
}
@end

namespace meshtools::platform {
namespace {

MeshToolsMenuTarget* menuTarget() {
    static MeshToolsMenuTarget* target = [[MeshToolsMenuTarget alloc] init];
    return target;
}

std::vector<std::filesystem::path> sampleFiles() {
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path sample_directory = "samples";
    if (!std::filesystem::exists(sample_directory)) {
        return paths;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(sample_directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string extension = entry.path().extension().string();
        if (extension == ".mt" || extension == ".stl") {
            paths.push_back(entry.path());
        }
    }

    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        return left.filename().string() < right.filename().string();
    });
    return paths;
}

}  // namespace

void initializeNativeMenu(const std::string& app_name) {
    @autoreleasepool {
        NSMenu* menu_bar = [[NSMenu alloc] init];

        NSMenuItem* app_menu_item = [[NSMenuItem alloc] init];
        [menu_bar addItem:app_menu_item];

        NSMenu* app_menu = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:app_name.c_str()]];
        NSMenuItem* settings_item = [[NSMenuItem alloc] initWithTitle:@"Settings…" action:@selector(openSettings:) keyEquivalent:@","];
        [settings_item setTarget:menuTarget()];
        [app_menu addItem:settings_item];
        [app_menu addItem:[NSMenuItem separatorItem]];

        NSString* quit_title = [NSString stringWithFormat:@"Quit %@", [NSString stringWithUTF8String:app_name.c_str()]];
        NSMenuItem* quit_item = [[NSMenuItem alloc] initWithTitle:quit_title action:@selector(quitApplication:) keyEquivalent:@"q"];
        [quit_item setTarget:menuTarget()];
        [app_menu addItem:quit_item];
        [app_menu_item setSubmenu:app_menu];

        NSMenuItem* file_menu_item = [[NSMenuItem alloc] init];
        [menu_bar addItem:file_menu_item];

        NSMenu* file_menu = [[NSMenu alloc] initWithTitle:@"File"];
        NSMenuItem* open_item = [[NSMenuItem alloc] initWithTitle:@"Open…" action:@selector(openDocument:) keyEquivalent:@"o"];
        [open_item setTarget:menuTarget()];
        [file_menu addItem:open_item];

        NSMenuItem* open_sample_item = [[NSMenuItem alloc] initWithTitle:@"Open Sample" action:nil keyEquivalent:@""];
        NSMenu* open_sample_menu = [[NSMenu alloc] initWithTitle:@"Open Sample"];
        for (const std::filesystem::path& sample_path : sampleFiles()) {
            MeshToolsSampleMenuItem* sample_item =
                [[MeshToolsSampleMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:sample_path.filename().string().c_str()]
                                                        action:@selector(openSample:)
                                                 keyEquivalent:@""];
            sample_item.samplePath = [NSString stringWithUTF8String:sample_path.string().c_str()];
            [sample_item setTarget:menuTarget()];
            [open_sample_menu addItem:sample_item];
        }
        [open_sample_item setSubmenu:open_sample_menu];
        [file_menu addItem:open_sample_item];

        NSMenuItem* save_item = [[NSMenuItem alloc] initWithTitle:@"Save" action:@selector(saveProject:) keyEquivalent:@"s"];
        [save_item setTarget:menuTarget()];
        [file_menu addItem:save_item];

        NSMenuItem* save_as_item =
            [[NSMenuItem alloc] initWithTitle:@"Save As…" action:@selector(saveProjectAs:) keyEquivalent:@"S"];
        [save_as_item setTarget:menuTarget()];
        [file_menu addItem:save_as_item];
        [file_menu_item setSubmenu:file_menu];

        NSMenuItem* edit_menu_item = [[NSMenuItem alloc] init];
        [menu_bar addItem:edit_menu_item];

        NSMenu* edit_menu = [[NSMenu alloc] initWithTitle:@"Edit"];
        NSMenuItem* undo_item = [[NSMenuItem alloc] initWithTitle:@"Undo" action:@selector(undoEdit:) keyEquivalent:@"z"];
        [undo_item setTarget:menuTarget()];
        [edit_menu addItem:undo_item];

        NSMenuItem* redo_item = [[NSMenuItem alloc] initWithTitle:@"Redo" action:@selector(redoEdit:) keyEquivalent:@"y"];
        [redo_item setTarget:menuTarget()];
        [edit_menu addItem:redo_item];
        [edit_menu_item setSubmenu:edit_menu];

        [NSApp setMainMenu:menu_bar];
    }
}

NativeMenuActions consumePendingNativeMenuActions() {
    const NativeMenuActions actions = g_pending_actions;
    g_pending_actions = NativeMenuActions{};
    return actions;
}

}  // namespace meshtools::platform
