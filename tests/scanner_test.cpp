#include "services/directory_scanner.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <atomic>
#include <cstdlib>
#include <memory>

int main() {
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        return EXIT_FAILURE;
    }

    QDir root(temporaryDirectory.path());
    if (!root.mkdir(QStringLiteral("данные"))) {
        return EXIT_FAILURE;
    }

    QFile file(root.filePath(QStringLiteral("данные/файл.bin")));
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray(4096, 'x')) != 4096) {
        return EXIT_FAILURE;
    }
    file.close();

#ifdef Q_OS_WIN
    const QString hardLinkPath = root.filePath(QStringLiteral("hard-link-copy.bin"));
    const QString originalPath = file.fileName();
    QFile alternateStream(originalPath + QStringLiteral(":disk-analyzer-stream"));
    if (!alternateStream.open(QIODevice::WriteOnly)
        || alternateStream.write(QByteArray(4096, 'y')) != 4096) {
        return EXIT_FAILURE;
    }
    alternateStream.close();
    if (!CreateHardLinkW(reinterpret_cast<LPCWSTR>(hardLinkPath.utf16()),
                         reinterpret_cast<LPCWSTR>(originalPath.utf16()), nullptr)) {
        return EXIT_FAILURE;
    }
#endif

    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    std::uint64_t processedItems = 0;
    std::uint64_t totalItems = 0;
    const auto progressCallback = [&](const std::uint64_t processed, const std::uint64_t total) {
        processedItems = processed;
        totalItems = total;
    };
    ScanResult result = DirectoryScanner(cancellation, progressCallback).scan(root.absolutePath());
    if (!result.root || result.root->sizeBytes == 0) {
        return EXIT_FAILURE;
    }
#ifdef Q_OS_WIN
    if (result.itemCount != 3 || processedItems != 3 || totalItems != 3) {
        return EXIT_FAILURE;
    }
#else
    if (result.itemCount != 2 || processedItems != 2 || totalItems != 2) {
        return EXIT_FAILURE;
    }
#endif
#ifdef Q_OS_WIN
    if (result.root->children.size() != 2 || result.root->children.front()->name != QStringLiteral("данные")) {
#else
    if (result.root->children.size() != 1 || result.root->children.front()->name != QStringLiteral("данные")) {
#endif
        return EXIT_FAILURE;
    }
    const FileNode& folder = *result.root->children.front();
    if (folder.children.size() != 1 || folder.children.front()->name != QStringLiteral("файл.bin")) {
        return EXIT_FAILURE;
    }
#ifdef Q_OS_WIN
    if (result.root->children.size() != 2) {
        return EXIT_FAILURE;
    }
    const FileNode& hardLink = *result.root->children.back();
    const FileNode& original = *folder.children.front();
    if (result.root->sizeBytes < 8192
        || hardLink.sizeBytes + original.sizeBytes != result.root->sizeBytes
        || hardLink.duplicateHardLink == original.duplicateHardLink) {
        return EXIT_FAILURE;
    }
#endif
    return EXIT_SUCCESS;
}
