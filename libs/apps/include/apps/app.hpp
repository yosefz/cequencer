#ifndef APPS_APP_HPP
#define APPS_APP_HPP

#include <config/config.hpp>
#include <config/configLoader.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

namespace apps {

class App {
public:
    virtual ~App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void initialize();
    void run();
    void stop();  // thread-safe

    bool isRunning() const { return running_.load(std::memory_order_acquire); }
    const std::string& name() const { return name_; }
    const std::string& basePath() const { return base_path_; }

protected:
    App(const std::string& name, const std::string& base_path);

    virtual void onInitialize() = 0;
    virtual void onRun() = 0;
    virtual void onShutdown() {}

    void createDirectories(const std::vector<std::string>& paths);

private:
    std::string name_;
    std::string base_path_;
    std::atomic<bool> running_{false};
    bool initialized_{false};
};

} // namespace apps

#endif // APPS_APP_HPP