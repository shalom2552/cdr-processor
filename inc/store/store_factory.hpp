#pragma once

#include "store/istore.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace cdrp {

/*
 * Maps a store type to a store. Holds the known stores and builds one on
 * demand. Single shared instance; register at startup, create per run.
 */
class StoreFactory {
public:
    using Creator = std::function<std::unique_ptr<IStore>()>;

    static StoreFactory& instance();
    ~StoreFactory() = default;

    StoreFactory(const StoreFactory&) = delete;
    StoreFactory& operator=(const StoreFactory&) = delete;

    /* Register a store under name, replacing any store already under it. */
    void registerStore(const std::string& name, Creator creator);

    /* True if a store is registered under name. */
    bool hasStore(const std::string& name) const;

    /* Build a new store for name, or nullptr if none is registered. */
    std::unique_ptr<IStore> createStore(const std::string& name) const;

private:
    StoreFactory();

private:
    std::unordered_map<std::string, Creator> m_stores;
};

} // namespace cdrp

