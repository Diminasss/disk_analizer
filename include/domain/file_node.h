#pragma once

#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

struct FileNode final {
    QString name;
    QString absolutePath;
    std::uint64_t sizeBytes{0};
    bool directory{false};
    FileNode* parent{nullptr};
    std::vector<std::unique_ptr<FileNode>> children;
};

struct ScanResult final {
    std::unique_ptr<FileNode> root;
    QString errorMessage;
    std::uint64_t itemCount{0};
    std::uint64_t inaccessibleCount{0};
    bool cancelled{false};
};
