#include "ui/main_window.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <exception>

namespace {
void useBundledQtPlugins() {
#ifdef Q_OS_WIN
    std::wstring executablePath(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
                                            static_cast<DWORD>(executablePath.size()));
    if (length == 0 || length == executablePath.size()) {
        return;
    }
    executablePath.resize(length);
    const QString pluginPath = QFileInfo(QString::fromStdWString(executablePath)).absolutePath();
    const QString nativePluginPath = QDir::toNativeSeparators(pluginPath);
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", reinterpret_cast<LPCWSTR>(nativePluginPath.utf16()));
#endif
}

void showStartupError(const QString& message) {
#ifdef Q_OS_WIN
    MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()), L"Ошибка запуска",
                MB_OK | MB_ICONERROR);
#else
    QMessageBox::critical(nullptr, QStringLiteral("Ошибка запуска"), message);
#endif
}
}

int main(int argc, char* argv[]) {
    try {
        useBundledQtPlugins();
        QApplication application(argc, argv);
        QApplication::setApplicationName(QStringLiteral("Disk Analyzer"));
        QApplication::setOrganizationName(QStringLiteral("DiskAnalyzer"));
        QApplication::setStyle(QStringLiteral("Fusion"));

        MainWindow window;
        window.show();
        return QApplication::exec();
    } catch (const std::exception& exception) {
        showStartupError(QStringLiteral("Не удалось запустить приложение:\n%1")
                             .arg(QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        showStartupError(QStringLiteral("Не удалось запустить приложение: неизвестная ошибка."));
    }
    return EXIT_FAILURE;
}
