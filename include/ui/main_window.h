#pragma once

#include "domain/file_node.h"

#include <QFutureWatcher>
#include <QMainWindow>

#include <atomic>
#include <memory>

class FileTreeModel;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QTreeView;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void chooseDirectory();
    void startScan(const QString& path);
    void finishScan();
    void revealSelection();
    void updateSelection();
    void updateProgress(std::uint64_t processedItems, std::uint64_t totalItems,
                        std::uint64_t scanGeneration);
    void animateProgress();

private:
    void buildInterface();
    void showError(const QString& message);
    void setScanning(bool scanning);

    FileTreeModel* model_{nullptr};
    QTreeView* tree_{nullptr};
    QLabel* pathLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QProgressBar* progress_{nullptr};
    QPushButton* chooseButton_{nullptr};
    QPushButton* revealButton_{nullptr};
    QTimer* progressAnimation_{nullptr};
    QFutureWatcher<std::shared_ptr<ScanResult>> watcher_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::uint64_t scanGeneration_{0};
    int targetProgress_{0};
};
