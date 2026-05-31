#include "services/directory_scanner.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

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

    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    std::uint64_t processedItems = 0;
    std::uint64_t totalItems = 0;
    const auto progressCallback = [&](const std::uint64_t processed, const std::uint64_t total) {
        processedItems = processed;
        totalItems = total;
    };
    ScanResult result = DirectoryScanner(cancellation, progressCallback).scan(root.absolutePath());
    if (!result.root || result.root->sizeBytes != 4096 || result.itemCount != 2) {
        return EXIT_FAILURE;
    }
    if (processedItems != 2 || totalItems != 2) {
        return EXIT_FAILURE;
    }
    if (result.root->children.size() != 1 || result.root->children.front()->name != QStringLiteral("данные")) {
        return EXIT_FAILURE;
    }
    const FileNode& folder = *result.root->children.front();
    if (folder.children.size() != 1 || folder.children.front()->name != QStringLiteral("файл.bin")) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
