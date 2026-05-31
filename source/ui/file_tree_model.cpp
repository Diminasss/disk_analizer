#include "ui/file_tree_model.h"

#include <QLocale>

#include <algorithm>
#include <array>

FileTreeModel::FileTreeModel(QObject* parent)
    : QAbstractItemModel(parent) {
}

void FileTreeModel::setRoot(std::unique_ptr<FileNode> root) {
    beginResetModel();
    root_ = std::move(root);
    endResetModel();
}

const FileNode* FileTreeModel::nodeForIndex(const QModelIndex& index) const {
    return index.isValid() ? static_cast<const FileNode*>(index.internalPointer()) : root_.get();
}

QModelIndex FileTreeModel::index(const int row, const int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) {
        return {};
    }
    FileNode* parentNode = nodeForParent(parent);
    if (!parentNode || row >= static_cast<int>(parentNode->children.size())) {
        return {};
    }
    return createIndex(row, column, parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex FileTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) {
        return {};
    }
    auto* node = static_cast<FileNode*>(child.internalPointer());
    FileNode* parentNode = node->parent;
    if (!parentNode || parentNode == root_.get()) {
        return {};
    }
    FileNode* grandParent = parentNode->parent;
    const auto iterator = std::ranges::find_if(grandParent->children, [parentNode](const auto& item) {
        return item.get() == parentNode;
    });
    return createIndex(static_cast<int>(std::distance(grandParent->children.begin(), iterator)), 0, parentNode);
}

int FileTreeModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0) {
        return 0;
    }
    const FileNode* node = nodeForIndex(parent);
    return node ? static_cast<int>(node->children.size()) : 0;
}

int FileTreeModel::columnCount(const QModelIndex&) const {
    return 3;
}

QVariant FileTreeModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid()) {
        return {};
    }
    const auto* node = static_cast<const FileNode*>(index.internalPointer());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0:
            return node->name;
        case 1:
            return formattedSize(node->sizeBytes);
        case 2:
            if (node->directory) {
                return tr("Папка");
            }
            return node->duplicateHardLink ? tr("Жесткая ссылка") : tr("Файл");
        default:
            return {};
        }
    }
    if (role == AbsolutePathRole) {
        return node->absolutePath;
    }
    if (role == DirectoryRole) {
        return node->directory;
    }
    if (role == SizeBytesRole) {
        return QVariant::fromValue<qulonglong>(node->sizeBytes);
    }
    if (role == SizeRatioRole) {
        if (!node->parent || node->parent->children.empty()) {
            return 1.0;
        }
        const auto largest = std::ranges::max_element(node->parent->children, {}, [](const auto& child) {
            return child->sizeBytes;
        });
        const std::uint64_t maxSize = (*largest)->sizeBytes;
        return maxSize == 0 ? 0.0 : static_cast<double>(node->sizeBytes) / static_cast<double>(maxSize);
    }
    if (role == Qt::ToolTipRole) {
        QString tooltip = QStringLiteral("%1\n%2").arg(node->absolutePath, formattedSize(node->sizeBytes));
        if (node->duplicateHardLink) {
            tooltip += tr("\nФизическое место уже учтено у другой жесткой ссылки");
        }
        return tooltip;
    }
    return {};
}

QVariant FileTreeModel::headerData(const int section, const Qt::Orientation orientation, const int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case 0:
        return tr("Название");
    case 1:
        return tr("Занято");
    case 2:
        return tr("Тип");
    default:
        return {};
    }
}

FileNode* FileTreeModel::nodeForParent(const QModelIndex& parent) const {
    return parent.isValid() ? static_cast<FileNode*>(parent.internalPointer()) : root_.get();
}

QString FileTreeModel::formattedSize(const std::uint64_t sizeBytes) {
    constexpr std::array units{"B", "KB", "MB", "GB", "TB", "PB"};
    auto value = static_cast<double>(sizeBytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }
    const int precision = unit == 0 || value >= 100.0 ? 0 : (value >= 10.0 ? 1 : 2);
    return QStringLiteral("%1 %2").arg(QLocale().toString(value, 'f', precision), QString::fromLatin1(units[unit]));
}
