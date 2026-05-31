#include "services/directory_scanner.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <algorithm>
#include <limits>
#include <utility>

DirectoryScanner::DirectoryScanner(std::shared_ptr<std::atomic_bool> cancellation,
                                   ProgressCallback progressCallback)
    : cancellation_(std::move(cancellation)),
      progressCallback_(std::move(progressCallback)) {
}

ScanResult DirectoryScanner::scan(const QString& rootPath) const {
    ScanResult result;
    QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        return result;
    }

    result.root = std::make_unique<FileNode>();
    result.root->name = QDir::toNativeSeparators(rootInfo.absoluteFilePath());
    result.root->absolutePath = rootInfo.absoluteFilePath();
    result.root->directory = true;
    reportProgress(0, 0);
    const std::uint64_t totalItems = countDirectory(result.root->absolutePath);
    std::uint64_t processedItems = 0;
    reportProgress(processedItems, totalItems);
    result.root->sizeBytes = scanDirectory(*result.root, result, totalItems, processedItems);
    reportProgress(processedItems, totalItems);
    result.cancelled = cancellation_->load(std::memory_order_relaxed);
    return result;
}

std::uint64_t DirectoryScanner::countDirectory(const QString& path) const {
    if (cancellation_->load(std::memory_order_relaxed)) {
        return 0;
    }

#ifdef Q_OS_WIN
    QString pattern = QDir::toNativeSeparators(path);
    if (!pattern.endsWith(u'\\')) {
        pattern += u'\\';
    }
    pattern += u'*';

    WIN32_FIND_DATAW entry{};
    const HANDLE handle = FindFirstFileExW(reinterpret_cast<LPCWSTR>(pattern.utf16()), FindExInfoBasic, &entry,
                                           FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    std::uint64_t count = 0;
    do {
        if (cancellation_->load(std::memory_order_relaxed)) {
            break;
        }
        const QString name = QString::fromWCharArray(entry.cFileName);
        if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
            continue;
        }
        ++count;
        const bool directory = (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool reparsePoint = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (directory && !reparsePoint) {
            count += countDirectory(QDir(path).filePath(name));
        }
    } while (FindNextFileW(handle, &entry));
    FindClose(handle);
    return count;
#else
    QDir directory(path);
    if (!directory.isReadable()) {
        return 0;
    }

    const auto filters = QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System;
    const QFileInfoList entries = directory.entryInfoList(filters, QDir::NoSort);
    std::uint64_t count = 0;
    for (const QFileInfo& entry : entries) {
        if (cancellation_->load(std::memory_order_relaxed)) {
            break;
        }
        ++count;
        if (entry.isDir() && !entry.isSymLink()) {
            count += countDirectory(entry.absoluteFilePath());
        }
    }
    return count;
#endif
}

std::uint64_t DirectoryScanner::scanDirectory(FileNode& node, ScanResult& result,
                                              const std::uint64_t totalItems,
                                              std::uint64_t& processedItems) const {
    if (cancellation_->load(std::memory_order_relaxed)) {
        return 0;
    }

#ifdef Q_OS_WIN
    QString pattern = QDir::toNativeSeparators(node.absolutePath);
    if (!pattern.endsWith(u'\\')) {
        pattern += u'\\';
    }
    pattern += u'*';

    WIN32_FIND_DATAW entry{};
    const HANDLE handle = FindFirstFileExW(reinterpret_cast<LPCWSTR>(pattern.utf16()), FindExInfoBasic, &entry,
                                           FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        ++result.inaccessibleCount;
        return 0;
    }

    std::uint64_t totalSize = 0;
    do {
        if (cancellation_->load(std::memory_order_relaxed)) {
            break;
        }
        const QString name = QString::fromWCharArray(entry.cFileName);
        if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
            continue;
        }

        auto child = std::make_unique<FileNode>();
        child->name = name;
        child->absolutePath = QDir(node.absolutePath).filePath(name);
        child->directory = (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        child->parent = &node;

        const bool reparsePoint = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (child->directory && !reparsePoint) {
            child->sizeBytes = scanDirectory(*child, result, totalItems, processedItems);
        } else if (!child->directory) {
            ULARGE_INTEGER fileSize{};
            fileSize.HighPart = entry.nFileSizeHigh;
            fileSize.LowPart = entry.nFileSizeLow;
            child->sizeBytes = fileSize.QuadPart;
        }

        totalSize += child->sizeBytes;
        node.children.push_back(std::move(child));
        ++result.itemCount;
        reportProgress(++processedItems, totalItems);
    } while (FindNextFileW(handle, &entry));
    FindClose(handle);
#else
    QDir directory(node.absolutePath);
    if (!directory.isReadable()) {
        ++result.inaccessibleCount;
        return 0;
    }

    const auto filters = QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System;
    const QFileInfoList entries = directory.entryInfoList(filters, QDir::NoSort);
    node.children.reserve(static_cast<std::size_t>(entries.size()));

    std::uint64_t totalSize = 0;
    for (const QFileInfo& entry : entries) {
        if (cancellation_->load(std::memory_order_relaxed)) {
            break;
        }

        auto child = std::make_unique<FileNode>();
        child->name = entry.fileName();
        child->absolutePath = entry.absoluteFilePath();
        child->directory = entry.isDir();
        child->parent = &node;

        if (entry.isSymLink()) {
            child->sizeBytes = child->directory ? 0 : static_cast<std::uint64_t>(entry.size());
        } else if (child->directory) {
            child->sizeBytes = scanDirectory(*child, result, totalItems, processedItems);
        } else {
            child->sizeBytes = static_cast<std::uint64_t>(entry.size());
        }

        totalSize += child->sizeBytes;
        node.children.push_back(std::move(child));
        ++result.itemCount;
        reportProgress(++processedItems, totalItems);
    }
#endif

    std::ranges::sort(node.children, {}, [](const auto& child) {
        return std::pair{!child->directory, std::numeric_limits<std::uint64_t>::max() - child->sizeBytes};
    });
    return totalSize;
}

void DirectoryScanner::reportProgress(const std::uint64_t processedItems,
                                      const std::uint64_t totalItems) const {
    if (!progressCallback_) {
        return;
    }
    constexpr std::uint64_t notificationInterval = 128;
    if (totalItems == 0 || processedItems == 0 || processedItems == totalItems
        || processedItems % notificationInterval == 0) {
        progressCallback_(processedItems, totalItems);
    }
}
