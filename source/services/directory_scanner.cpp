#include "services/directory_scanner.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>

#ifdef Q_OS_WIN
namespace {
QString extendedWindowsPath(const QString& path) {
    QString nativePath = QDir::toNativeSeparators(path);
    if (nativePath.startsWith(QStringLiteral("\\\\?\\"))) {
        return nativePath;
    }
    if (nativePath.startsWith(QStringLiteral("\\\\"))) {
        return QStringLiteral("\\\\?\\UNC\\") + nativePath.sliced(2);
    }
    return QStringLiteral("\\\\?\\") + nativePath;
}

struct WindowsFileMetadata final {
    WindowsFileIdentity identity;
    std::uint64_t allocatedSize{0};
};

std::optional<std::uint64_t> allocatedSizeForPath(const QString& nativePath) {
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                      FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    FILE_STANDARD_INFO information{};
    const bool success = GetFileInformationByHandleEx(handle, FileStandardInfo,
                                                       &information, sizeof(information));
    CloseHandle(handle);
    if (!success || information.AllocationSize.QuadPart < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(information.AllocationSize.QuadPart);
}

std::optional<std::uint64_t> allocatedSizeIncludingStreams(const QString& nativePath,
                                                           const std::uint64_t fallbackSize) {
    WIN32_FIND_STREAM_DATA streamData{};
    const HANDLE streamHandle = FindFirstStreamW(reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                                 FindStreamInfoStandard, &streamData, 0);
    if (streamHandle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_HANDLE_EOF || error == ERROR_INVALID_PARAMETER
            || error == ERROR_NOT_SUPPORTED) {
            return fallbackSize;
        }
        return std::nullopt;
    }

    std::uint64_t totalSize = 0;
    do {
        const QString streamPath = nativePath + QString::fromWCharArray(streamData.cStreamName);
        const std::optional<std::uint64_t> streamSize = allocatedSizeForPath(streamPath);
        if (!streamSize) {
            FindClose(streamHandle);
            return std::nullopt;
        }
        totalSize += *streamSize;
    } while (FindNextStreamW(streamHandle, &streamData));

    const DWORD error = GetLastError();
    FindClose(streamHandle);
    return error == ERROR_HANDLE_EOF ? std::optional<std::uint64_t>{totalSize} : std::nullopt;
}

std::optional<WindowsFileMetadata> fileMetadata(const QString& path) {
    const QString nativePath = extendedWindowsPath(path);
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                      FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    BY_HANDLE_FILE_INFORMATION information{};
    FILE_STANDARD_INFO standardInformation{};
    if (!GetFileInformationByHandle(handle, &information)
        || !GetFileInformationByHandleEx(handle, FileStandardInfo,
                                         &standardInformation, sizeof(standardInformation))) {
        CloseHandle(handle);
        return std::nullopt;
    }
    CloseHandle(handle);

    if (standardInformation.AllocationSize.QuadPart < 0) {
        return std::nullopt;
    }
    const auto allocationSize = static_cast<std::uint64_t>(standardInformation.AllocationSize.QuadPart);
    const std::optional<std::uint64_t> totalAllocationSize =
        allocatedSizeIncludingStreams(nativePath, allocationSize);
    if (!totalAllocationSize) {
        return std::nullopt;
    }

    WindowsFileMetadata metadata;
    metadata.identity.volumeSerialNumber = information.dwVolumeSerialNumber;
    metadata.identity.fileIndex = (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32)
        | information.nFileIndexLow;
    metadata.allocatedSize = *totalAllocationSize;
    return metadata;
}
}

std::size_t WindowsFileIdentityHash::operator()(const WindowsFileIdentity& identity) const noexcept {
    const std::size_t first = std::hash<std::uint64_t>{}(identity.volumeSerialNumber);
    const std::size_t second = std::hash<std::uint64_t>{}(identity.fileIndex);
    return first ^ (second + 0x9e3779b9U + (first << 6) + (first >> 2));
}
#endif

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
#ifdef Q_OS_WIN
    std::unordered_set<WindowsFileIdentity, WindowsFileIdentityHash> countedFiles;
    result.root->sizeBytes = scanDirectory(*result.root, result, totalItems, processedItems, countedFiles);
#else
    result.root->sizeBytes = scanDirectory(*result.root, result, totalItems, processedItems);
#endif
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
                                              std::uint64_t& processedItems
#ifdef Q_OS_WIN
                                              , std::unordered_set<WindowsFileIdentity,
                                                                   WindowsFileIdentityHash>& countedFiles
#endif
                                              ) const {
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
            child->sizeBytes = scanDirectory(*child, result, totalItems, processedItems, countedFiles);
        } else if (!child->directory && !reparsePoint) {
            const std::optional<WindowsFileMetadata> metadata = fileMetadata(child->absolutePath);
            if (!metadata) {
                ++result.inaccessibleCount;
            } else if (!countedFiles.insert(metadata->identity).second) {
                child->duplicateHardLink = true;
            } else {
                child->sizeBytes = metadata->allocatedSize;
            }
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
