#include "app/Application.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        strategy::Application application;
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
