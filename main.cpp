#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include "MainWindow.h"
#include "GDALManager.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 设置应用程序信息
    app.setApplicationName("ProjectSSA");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("YourCompany");

    qDebug() << "==========================================";
    qDebug() << "应用程序启动...";

    try {
        // 尝试初始化GDAL，但不让失败阻止程序运行
        qDebug() << "开始初始化GDAL管理器...";
        GDALManager::initialize();

        if (GDALManager::isInitialized()) {
            qDebug() << "GDAL初始化成功";
        }
        else {
            qWarning() << "GDAL初始化失败，但程序将继续运行";
            qWarning() << "某些图像处理功能可能不可用";
        }

    }
    catch (const std::exception& e) {
        qWarning() << "GDAL初始化异常:" << e.what();
        qWarning() << "程序将在有限功能模式下运行";
    }
    catch (...) {
        qWarning() << "GDAL初始化发生未知异常";
        qWarning() << "程序将在有限功能模式下运行";
    }

    // 无论GDAL是否成功，都继续创建主窗口
    qDebug() << "创建主窗口...";

    MainWindow mainWindow;

    // 如果GDAL初始化失败，显示警告但不阻止程序运行
    if (!GDALManager::isInitialized()) {
        QMessageBox::warning(&mainWindow, "GDAL初始化警告",
            "GDAL库初始化失败，某些功能可能不可用：\n\n"
            "• 图像文件读取可能受限\n"
            "• 几何配准功能可能无法使用\n"
            "• 图像融合功能可能无法使用\n\n"
            "建议：\n"
            "1. 检查GDAL库是否正确安装\n"
            "2. 检查环境变量配置\n"
            "3. 联系技术支持\n\n"
            "点击确定继续使用程序的其他功能。",
            QMessageBox::Ok);
    }

    // 显示主窗口
    mainWindow.show();

    qDebug() << "主窗口已显示，应用程序就绪";
    qDebug() << "==========================================";

    // 运行应用程序事件循环
    int result = app.exec();

    // 程序退出时清理GDAL资源
    qDebug() << "应用程序即将退出，清理资源...";

    try {
        GDALManager::cleanup();
    }
    catch (...) {
        qDebug() << "GDAL清理过程中发生异常，忽略";
    }

    qDebug() << "应用程序退出，返回码:" << result;
    return result;
}