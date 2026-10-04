#include "hro_config.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        ("hro-display-config-" + std::to_string(getpid()) + ".ini");
    auto check = [](bool ok, const char* message) {
        if (!ok) { std::cerr << message << '\n'; std::exit(1); }
    };
    HroConfig config; config.setDefaults();
    config.observer = "Station"; config.display_level_db = -9;
    check(config.save(path.string()), "save adjusted config");
    HroConfig loaded;
    check(loaded.load(path.string()) && loaded.display_level_db == -9 &&
        loaded.observer == "Station", "appearance and station round trip");
    // Simulate a settings-page update without losing the local display preference.
    loaded.location = "New location";
    check(loaded.save(path.string()), "save station settings");
    check(config.load(path.string()) && config.display_level_db == -9,
        "station save preserves appearance");
    std::ifstream in(path); std::ostringstream contents; contents << in.rdbuf(); in.close();
    auto legacy = contents.str();
    const auto start = legacy.find("[display]\n");
    const auto end = legacy.find("[screenshot]", start);
    legacy.erase(start, end - start);
    { std::ofstream out(path); out << legacy; }
    check(loaded.load(path.string()) && loaded.display_level_db == 0 &&
        loaded.observer == "Station", "legacy config defaults to unchanged colours");
    for (int level : {-30, 30}) {
        loaded.display_level_db = level;
        check(loaded.save(path.string()) && config.load(path.string()) &&
            config.display_level_db == level, "boundary round trip");
    }
    loaded.display_level_db = 31;
    check(!loaded.save(path.string()), "invalid appearance rejected");
    check(config.load(path.string()) && config.display_level_db == 30,
        "invalid save retains valid file");
    std::filesystem::remove(path);
}
