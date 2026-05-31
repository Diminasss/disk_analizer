#pragma once

#include "domain/file_node.h"

#include <QAbstractItemModel>

#include <memory>

class FileTreeModel final : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Roles {
        AbsolutePathRole = Qt::UserRole + 1,
        SizeRatioRole,
        DirectoryRole,
        SizeBytesRole
    };

    explicit FileTreeModel(QObject* parent = nullptr);

    void setRoot(std::unique_ptr<FileNode> root);
    [[nodiscard]] const FileNode* nodeForIndex(const QModelIndex& index) const;

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

private:
    [[nodiscard]] FileNode* nodeForParent(const QModelIndex& parent) const;
    [[nodiscard]] static QString formattedSize(std::uint64_t sizeBytes);

    std::unique_ptr<FileNode> root_;
};
