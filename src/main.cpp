#include <cstdlib>
#include <exception>
#include <iostream>

#include "meshtools/app/EditorApplication.h"

int main() {
    try {
        meshtools::app::EditorApplication application;
        return application.run();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
