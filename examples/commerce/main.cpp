#include <designer/commerce/archive.hpp>
#include <designer/persistence/file_store.hpp>
#include <iostream>
#include <stdexcept>

namespace shop = designer::commerce;
namespace eco = designer::economy;
namespace inv = designer::inventory;
namespace store = designer::persistence;

shop::ProductCatalog products(eco::Amount price = 100, std::uint32_t version = 1) {
    return shop::ProductCatalog({{100, version, true, {{{1, price}}}, {{{2, 1}}, {{1001, 5}}}}});
}
shop::Shop create_shop() {
    eco::Wallet wallet(eco::CurrencyCatalog({{1, 1000}, {2, 100}}));
    if (wallet.credit(1, 500) != eco::Error::none) throw std::runtime_error("Invalid demo funds");
    auto economy = std::make_unique<eco::Economy>(std::move(wallet),
        std::vector<eco::Container>{{1, inv::Inventory(3, inv::ItemCatalog({{1001, 20}}))}});
    return shop::Shop(products(), std::move(economy));
}
int main(int argc, char** argv) {
    const bool load_only = argc == 3 && std::string_view(argv[2]) == "--load";
    if (argc != 2 && !load_only) {
        std::cerr << "Usage: designer_commerce_example <example-save-path> [--load]\n"
                     "Without --load this replaces the specified file.\n";
        return 1;
    }
    auto commerce = create_shop();
    const shop::PurchaseRequest original{42, 100, 2, 1};
    if (load_only) {
        // Demonstrate that a removed product does not prevent historical restoration or replay.
        commerce.replace_catalog(shop::ProductCatalog({}));
        std::string bytes;
        const auto loaded = store::load_file(argv[1], bytes);
        if (loaded != store::FileError::none) { std::cerr << store::file_error_name(loaded) << '\n'; return 1; }
        shop::Snapshot snapshot;
        const auto decoded = shop::decode_archive(bytes, snapshot);
        if (decoded != shop::ArchiveError::none) { std::cerr << shop::archive_error_name(decoded) << '\n'; return 1; }
        const auto restored = commerce.restore(snapshot);
        if (restored != shop::Error::none) { std::cerr << shop::error_name(restored) << '\n'; return 1; }
    } else {
        const auto first = commerce.purchase(original);
        if (first.error != shop::Error::none || first.replayed || !first.receipt) return 1;
        std::cout << "Bought 2 bundles (10 potions) for 200 gold.\n";
        commerce.replace_catalog(products(150, 2));
        const auto repeated = commerce.purchase(original);
        if (!repeated.replayed || !(repeated.receipt == first.receipt)) return 1;
        const auto second = commerce.purchase({43, 100, 1, 1});
        if (second.error != shop::Error::none || second.receipt->product_version != 2) return 1;
        std::cout << "Old request kept its original price; new purchase charged 150 gold.\n";
        const auto insufficient = commerce.purchase({44, 100, 2, 1});
        if (insufficient.error != shop::Error::economy_failure || insufficient.economy_error != eco::Error::insufficient_funds) return 1;
    }
    std::string before, after;
    if (shop::encode_archive(commerce.snapshot(), before) != shop::ArchiveError::none) return 1;
    const auto repeated = commerce.purchase(original);
    if (shop::encode_archive(commerce.snapshot(), after) != shop::ArchiveError::none || before != after ||
        repeated.error != shop::Error::none || !repeated.replayed || repeated.receipt->product_version != 1 ||
        repeated.receipt->settlement.transaction.cost.currencies[0].amount != 200) return 1;
    if (commerce.economy().wallet().balance(1) != 150 || commerce.economy().wallet().balance(2) != 3 ||
        commerce.snapshot().purchases.size() != 2) return 1;
    std::cout << "Gold=150, bonus points=3, successful purchases=2.\n";
    if (load_only) std::cout << "Restart replay succeeded even though the product was removed.\n";
    else {
        const auto saved = store::save_atomic(argv[1], after);
        if (saved.error != store::FileError::none) {
            std::cerr << store::file_error_name(saved.error) << "; committed=" << saved.committed << '\n'; return 1;
        }
        std::cout << "Saved assets, economic receipts and purchase intents together.\n";
    }
}
