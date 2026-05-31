#include "ui/size_bar_delegate.h"

#include "ui/file_tree_model.h"

#include <QPainter>

void SizeBarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const {
    if (index.column() == 0) {
        const double ratio = index.data(FileTreeModel::SizeRatioRole).toDouble();
        const uint hash = qHash(index.data(FileTreeModel::AbsolutePathRole).toString());
        const QColor color = QColor::fromHsv(static_cast<int>(hash % 360), 185, 238, 165);
        QRect bar = option.rect.adjusted(1, 3, -1, -3);
        bar.setWidth(static_cast<int>(bar.width() * ratio));
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRoundedRect(bar, 3.0, 3.0);
        painter->restore();
    }
    QStyledItemDelegate::paint(painter, option, index);
}
