#include <designer/economy/archive.hpp>
#include <designer/persistence/file_store.hpp>
#include <iostream>
#include <stdexcept>

namespace eco = designer::economy;
namespace inv = designer::inventory;
namespace store = designer::persistence;

eco::Economy create_economy() {
    eco::Wallet wallet(eco::CurrencyCatalog({{1, 1000}, {2, 100}}));
    if (wallet.credit(1, 500) != eco::Error::none || wallet.credit(2, 90) != eco::Error::none)
        throw std::runtime_error("Invalid demo initial balances");
    inv::ItemCatalog rules({{1001, 20}, {2001, 1}});
    return eco::Economy(std::move(wallet), {{1, inv::Inventory(3, rules)}, {2, inv::Inventory(6, rules)}});
}
eco::Transaction request() { return {42, 1, {{{1, 100}}}, {{{2, 5}}, {{1001, 5}}}}; }

int main(int argc, char** argv) {
    const bool load_only = argc == 3 && std::string_view(argv[2]) == "--load";
    if (argc != 2 && !load_only) {
        std::cerr << "Usage: designer_economy_example <example-save-path> [--load]\n"
                     "Without --load this replaces the specified file.\n";
        return 1;
    }
    auto economy = create_economy();
    std::string bytes;
    if (load_only) {
        const auto loaded = store::load_file(argv[1], bytes);
        if (loaded != store::FileError::none) { std::cerr << store::file_error_name(loaded) << '\n'; return 1; }
        eco::Snapshot decoded;
        const auto parsed = eco::decode_archive(bytes, decoded);
        if (parsed != eco::ArchiveError::none) { std::cerr << eco::archive_error_name(parsed) << '\n'; return 1; }
        const auto restored = economy.restore(decoded);
        if (restored != eco::Error::none) { std::cerr << eco::error_name(restored) << '\n'; return 1; }
    } else {
        const auto purchased = economy.execute(request());
        if (purchased.error != eco::Error::none || !purchased.receipt || purchased.replayed) return 1;
        // The item reward cannot fit. Currency debit must not commit.
        const auto failed = economy.execute({43, 1, {{{1, 50}}}, {{}, {{1001, 100}}}});
        if (failed.error != eco::Error::inventory_failure || economy.wallet().balance(1) != 400) return 1;
        std::cout << "Purchase succeeded; oversized reward failed without charging.\n";
    }
    std::string before;
    if (eco::encode_archive(economy.snapshot(), before) != eco::ArchiveError::none) return 1;
    const auto replay = economy.execute(request());
    std::string after;
    if (eco::encode_archive(economy.snapshot(), after) != eco::ArchiveError::none || before != after ||
        replay.error != eco::Error::none || !replay.replayed || !replay.receipt || replay.receipt->sequence != 1) return 1;
    std::cout << "Request 42 replayed without a second charge" << (load_only ? " after restart.\n" : ".\n");
    std::cout << "Gold=" << economy.wallet().balance(1) << ", points=" << economy.wallet().balance(2)
              << ", successful requests=" << economy.history_size() << '\n';
    if (economy.wallet().balance(1) != 400 || economy.wallet().balance(2) != 95 || economy.history_size() != 1) return 1;
    if (!load_only) {
        const auto saved = store::save_atomic(argv[1], after);
        if (saved.error != store::FileError::none) {
            std::cerr << store::file_error_name(saved.error) << "; committed=" << saved.committed << '\n'; return 1;
        }
        std::cout << "Wallet, all containers and request receipt saved together.\n";
    }
}
