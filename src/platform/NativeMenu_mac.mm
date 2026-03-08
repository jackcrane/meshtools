#include "meshtools/platform/NativeMenu.h"

#import <Cocoa/Cocoa.h>

using meshtools::platform::NativeMenuActions;

static NativeMenuActions g_pending_actions;

@interface MeshToolsMenuTarget : NSObject
- (void)openMesh:(id)sender;
- (void)openSettings:(id)sender;
- (void)quitApplication:(id)sender;
@end

@implementation MeshToolsMenuTarget
- (void)openMesh:(id)sender {
    (void)sender;
    g_pending_actions.open_mesh = true;
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
        NSMenuItem* open_item = [[NSMenuItem alloc] initWithTitle:@"Open…" action:@selector(openMesh:) keyEquivalent:@"o"];
        [open_item setTarget:menuTarget()];
        [file_menu addItem:open_item];
        [file_menu_item setSubmenu:file_menu];

        [NSApp setMainMenu:menu_bar];
    }
}

NativeMenuActions consumePendingNativeMenuActions() {
    const NativeMenuActions actions = g_pending_actions;
    g_pending_actions = NativeMenuActions{};
    return actions;
}

}  // namespace meshtools::platform
