#include "apps/app.hpp"

#include <filesystem>
#include <iostream>

namespace apps {

    App::App(const std::string& name, const std::string& base_path)
        : name_(name)
        , base_path_(base_path) {
    }

    void App::initialize() {
        if (initialized_) return;

        std::cout << "Initializing " << name_ << "...\n";

        onInitialize();

        initialized_ = true;
        std::cout << name_ << " initialized.\n";
    }

    void App::run() {
        if (!initialized_) {
            initialize();
        }

        running_.store(true, std::memory_order_release);
        std::cout << name_ << " running...\n";

        onRun();

        running_.store(false, std::memory_order_release);
        std::cout << name_ << " stopped.\n";

        onShutdown();
    }

    void App::stop() {
        running_.store(false, std::memory_order_release);
    }

    void App::createDirectories(const std::vector<std::string>& paths) {
        for (const auto& path : paths) {
            std::filesystem::create_directories(path);
        }
    }

} // namespace apps