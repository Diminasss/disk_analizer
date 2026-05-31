#include "ui/main_window.h"

#include "services/directory_scanner.h"
#include "ui/animated_progress_bar.h"
#include "ui/file_tree_model.h"
#include "ui/size_bar_delegate.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <exception>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    buildInterface();
    connect(&watcher_, &QFutureWatcher<std::shared_ptr<ScanResult>>::finished, this, &MainWindow::finishScan);
}

MainWindow::~MainWindow() {
    if (cancellation_) {
        cancellation_->store(true, std::memory_order_relaxed);
    }
    watcher_.waitForFinished();
}

void MainWindow::chooseDirectory() {
    try {
        const QString initialPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        const QString path = QFileDialog::getExistingDirectory(this, tr("Выберите диск или папку"), initialPath,
                                                               QFileDialog::ShowDirsOnly);
        if (!path.isEmpty()) {
            startScan(path);
        }
    } catch (const std::exception& exception) {
        showError(tr("Не удалось выбрать папку: %1").arg(QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        showError(tr("Не удалось выбрать папку: неизвестная ошибка."));
    }
}

void MainWindow::startScan(const QString& path) {
    try {
        if (watcher_.isRunning()) {
            cancellation_->store(true, std::memory_order_relaxed);
            watcher_.waitForFinished();
        }

        cancellation_ = std::make_shared<std::atomic_bool>(false);
        pathLabel_->setText(QDir::toNativeSeparators(path));
        model_->setRoot({});
        setScanning(true);

        const auto cancellation = cancellation_;
        const std::uint64_t scanGeneration = ++scanGeneration_;
        const auto progressCallback = [this, scanGeneration](const std::uint64_t processedItems,
                                                             const std::uint64_t totalItems) {
            QMetaObject::invokeMethod(this, [this, processedItems, totalItems, scanGeneration] {
                updateProgress(processedItems, totalItems, scanGeneration);
            }, Qt::QueuedConnection);
        };
        watcher_.setFuture(QtConcurrent::run([path, cancellation, progressCallback] {
            auto result = std::make_shared<ScanResult>();
            try {
                *result = DirectoryScanner(cancellation, progressCallback).scan(path);
            } catch (const std::exception& exception) {
                result->errorMessage = QObject::tr("Ошибка сканирования: %1")
                                           .arg(QString::fromLocal8Bit(exception.what()));
            } catch (...) {
                result->errorMessage = QObject::tr("Ошибка сканирования: неизвестная ошибка.");
            }
            return result;
        }));
    } catch (const std::exception& exception) {
        setScanning(false);
        showError(tr("Не удалось начать сканирование: %1").arg(QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        setScanning(false);
        showError(tr("Не удалось начать сканирование: неизвестная ошибка."));
    }
}

void MainWindow::finishScan() {
    try {
        const std::shared_ptr<ScanResult> result = watcher_.result();
        setScanning(false);
        if (!result->errorMessage.isEmpty()) {
            showError(result->errorMessage);
            return;
        }
        if (result->cancelled) {
            statusLabel_->setText(tr("Сканирование отменено"));
            return;
        }
        if (!result->root) {
            showError(tr("Выбранная папка недоступна."));
            return;
        }

        const std::uint64_t items = result->itemCount;
        const std::uint64_t inaccessible = result->inaccessibleCount;
        model_->setRoot(std::move(result->root));
        statusLabel_->setText(tr("Просканировано объектов: %1. Недоступно: %2").arg(items).arg(inaccessible));
    } catch (const std::exception& exception) {
        setScanning(false);
        showError(tr("Не удалось завершить сканирование: %1").arg(QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        setScanning(false);
        showError(tr("Не удалось завершить сканирование: неизвестная ошибка."));
    }
}

void MainWindow::revealSelection() {
    try {
        const QModelIndex index = tree_->currentIndex();
        if (!index.isValid()) {
            return;
        }
        const QString path = index.data(FileTreeModel::AbsolutePathRole).toString();
        const bool directory = index.data(FileTreeModel::DirectoryRole).toBool();
        const QString target = directory ? path : QFileInfo(path).absolutePath();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(target))) {
            showError(tr("Не удалось открыть расположение:\n%1").arg(target));
        }
    } catch (const std::exception& exception) {
        showError(tr("Не удалось открыть расположение: %1").arg(QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        showError(tr("Не удалось открыть расположение: неизвестная ошибка."));
    }
}

void MainWindow::updateSelection() {
    revealButton_->setEnabled(tree_->currentIndex().isValid());
}

void MainWindow::updateProgress(const std::uint64_t processedItems,
                                const std::uint64_t totalItems,
                                const std::uint64_t scanGeneration) {
    if (scanGeneration != scanGeneration_ || !watcher_.isRunning()) {
        return;
    }
    if (totalItems == 0) {
        progress_->setRange(0, 0);
        progress_->setFormat(tr("Подготовка..."));
        return;
    }

    progress_->setRange(0, 1000);
    progress_->setFormat(QStringLiteral("%p%"));
    const auto ratio = static_cast<long double>(processedItems) / static_cast<long double>(totalItems);
    targetProgress_ = std::clamp(static_cast<int>(ratio * 1000.0L), 0, 1000);
}

void MainWindow::animateProgress() {
    if (progress_->maximum() == 0 || progress_->value() >= targetProgress_) {
        return;
    }
    const int distance = targetProgress_ - progress_->value();
    progress_->setValue(progress_->value() + std::max(1, distance / 7));
}

void MainWindow::buildInterface() {
    setWindowTitle(tr("Анализатор диска"));
    resize(1120, 760);

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* toolbar = new QHBoxLayout();
    chooseButton_ = new QPushButton(tr("Выбрать диск или папку"), content);
    revealButton_ = new QPushButton(tr("Открыть расположение"), content);
    revealButton_->setEnabled(false);
    pathLabel_ = new QLabel(tr("Выберите папку для начала работы"), content);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    toolbar->addWidget(chooseButton_);
    toolbar->addWidget(revealButton_);
    toolbar->addWidget(pathLabel_, 1);

    model_ = new FileTreeModel(this);
    tree_ = new QTreeView(content);
    tree_->setModel(model_);
    tree_->setItemDelegate(new SizeBarDelegate(tree_));
    tree_->setAlternatingRowColors(true);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setSortingEnabled(false);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->resizeSection(1, 130);
    tree_->header()->resizeSection(2, 100);

    auto* footer = new QHBoxLayout();
    statusLabel_ = new QLabel(tr("Готово"), content);
    progress_ = new AnimatedProgressBar(content);
    progress_->setRange(0, 0);
    progress_->setTextVisible(true);
    progress_->setFixedWidth(230);
    progress_->hide();
    progressAnimation_ = new QTimer(this);
    progressAnimation_->setInterval(35);
    connect(progressAnimation_, &QTimer::timeout, this, &MainWindow::animateProgress);
    footer->addWidget(statusLabel_, 1);
    footer->addWidget(progress_);

    layout->addLayout(toolbar);
    layout->addWidget(tree_, 1);
    layout->addLayout(footer);
    setCentralWidget(content);

    setStyleSheet(QStringLiteral(
        "QWidget { color: black; }"
        "QMainWindow { background: #f4f6f8; }"
        "QLabel { color: black; }"
        "QTreeView { background: white; color: black; border: 1px solid #cfd6dc; alternate-background-color: #f7f9fa; }"
        "QTreeView::item { color: black; height: 25px; }"
        "QTreeView::item:selected { color: black; background: #b8d8e3; }"
        "QHeaderView::section { background: #e7ecef; border: 0; border-right: 1px solid #cfd6dc;"
        " color: black; padding: 6px; font-weight: 600; }"
        "QPushButton { background: #77bfd6; color: black; border: 0; border-radius: 4px; padding: 7px 12px; }"
        "QPushButton:hover { background: #98d3e3; }"
        "QPushButton:disabled { background: #aeb8bd; }"
        "QProgressBar { color: black; background: white; border: 1px solid #9caeb5; border-radius: 3px;"
        " text-align: center; min-height: 18px; }"
        "QProgressBar::chunk { background: #58b8d5; border-radius: 2px; }"));

    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseDirectory);
    connect(revealButton_, &QPushButton::clicked, this, &MainWindow::revealSelection);
    connect(tree_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateSelection);
    connect(tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.data(FileTreeModel::DirectoryRole).toBool()) {
            revealSelection();
        }
    });
}

void MainWindow::setScanning(const bool scanning) {
    chooseButton_->setEnabled(!scanning);
    tree_->setEnabled(!scanning);
    progress_->setVisible(scanning);
    if (scanning) {
        targetProgress_ = 0;
        progress_->setRange(0, 0);
        progress_->setFormat(tr("Подготовка..."));
        progressAnimation_->start();
        revealButton_->setEnabled(false);
        statusLabel_->setText(tr("Сканирование..."));
    } else {
        progressAnimation_->stop();
    }
}

void MainWindow::showError(const QString& message) {
    statusLabel_->setText(message);
    QMessageBox::critical(this, tr("Ошибка"), message);
}
