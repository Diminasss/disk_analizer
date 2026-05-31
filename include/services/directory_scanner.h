#pragma once

#include "domain/file_node.h"

#include <QString>

#include <atomic>
#include <functional>
#include <memory>

class DirectoryScanner final {
public:
    using ProgressCallback = std::function<void(std::uint64_t processed, std::uint64_t total)>;

    explicit DirectoryScanner(std::shared_ptr<std::atomic_bool> cancellation,
                              ProgressCallback progressCallback = {});

    [[nodiscard]] ScanResult scan(const QString& rootPath) const;

private:
    [[nodiscard]] std::uint64_t countDirectory(const QString& path) const;
    [[nodiscard]] std::uint64_t scanDirectory(FileNode& node, ScanResult& result,
                                              std::uint64_t totalItems,
                                              std::uint64_t& processedItems) const;
    void reportProgress(std::uint64_t processedItems, std::uint64_t totalItems) const;

    std::shared_ptr<std::atomic_bool> cancellation_;
    ProgressCallback progressCallback_;
};
