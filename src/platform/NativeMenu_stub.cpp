#include "meshtools/platform/NativeMenu.h"

namespace meshtools::platform {

void initializeNativeMenu(const std::string& /*app_name*/) {}

NativeMenuActions consumePendingNativeMenuActions() {
    return NativeMenuActions{};
}

}  // namespace meshtools::platform
