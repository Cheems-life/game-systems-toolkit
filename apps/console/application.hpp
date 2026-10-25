#pragma once
#include <designer/configuration/bundle.hpp>
#include <designer/configuration/dialogue_content.hpp>
#include <designer/player/player.hpp>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace designer::app {
// Application composition only; all gameplay mutations go through Player.
class Application {
public:
    Application(const std::filesystem::path& content, std::filesystem::path save);
    int run(std::istream& input, bool scripted);
private:
    bool dispatch(const std::vector<std::string>& words);
    void show_status() const;
    void show_shop() const;
    void show_quests() const;
    void show_dialogue() const;
    bool save_game();
    bool load_game();
    std::optional<dialogue::SessionId> active_session() const;
    std::uint64_t request_id();
    void reset_request_cursor(const player::PlayerSnapshot& state);

    std::unique_ptr<const configuration::Bundle> content_;
    std::unique_ptr<const configuration::TextTable> texts_;
    dialogue::Catalog dialogue_catalog_;
    std::unique_ptr<player::Player> player_;
    std::filesystem::path save_path_;
    std::uint64_t next_request_ = 1;
    bool dirty_ = false;
    bool quit_ = false;
};
} // namespace designer::app
