// 修改GDALManager.cpp - 更加宽松的初始化策略

#include "GDALManager.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QMutexLocker>
#include <QMessageBox>

// 静态成员初始化
bool GDALManager::initialized_ = false;
QMutex GDALManager::mutex_;

GDALManager::GDALManager()
{
    qDebug() << "GDALManager 实例创建";
}

GDALManager::~GDALManager()
{
    qDebug() << "GDALManager 开始析构";
    // 不在这里调用cleanup，让程序正常退出
    qDebug() << "GDALManager 析构完成";
}

void GDALManager::initialize()
{
    QMutexLocker locker(&mutex_);

    if (initialized_) {
        qDebug() << "GDAL已经初始化，跳过重复初始化";
        return;
    }

    qDebug() << "开始初始化GDAL...";

    try {
        // 1. 设置GDAL配置选项（更加宽松）
        CPLSetConfigOption("GDAL_FILENAME_IS_UTF8", "YES");
        CPLSetConfigOption("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR");
        CPLSetConfigOption("CPL_DEBUG", "OFF");  // 关闭调试输出

        qDebug() << "GDAL默认配置选项设置完成";

        // 2. 注册所有GDAL驱动
        GDALAllRegister();

        QString version = GDALVersionInfo("RELEASE_NAME");
        int driverCount = GDALGetDriverCount();

        qDebug() << "GDAL版本:" << version;
        qDebug() << "可用驱动数量:" << driverCount;

        // 3. 设置缓存大小（可选）
        GDALSetCacheMax(256 * 1024 * 1024); // 256MB
        qDebug() << "GDAL缓存大小设置为: 256 MB";

        // 4. 验证关键驱动（警告模式，不强制退出）
        bool hasBasicDrivers = validateCriticalDriversLenient();

        if (hasBasicDrivers) {
            qDebug() << "GDAL初始化成功，至少部分关键驱动可用";
        }
        else {
            qWarning() << "GDAL初始化完成，但可能缺少某些驱动";
            qWarning() << "这可能影响部分功能，但程序可以继续运行";
        }

        // 5. 设置错误处理（非阻塞）
        CPLSetErrorHandler(customErrorHandler);

        initialized_ = true;
        qDebug() << "GDAL管理器初始化完成";

    }
    catch (const std::exception& e) {
        qWarning() << "GDAL初始化异常，但将继续运行:" << e.what();
        initialized_ = false;
    }
    catch (...) {
        qWarning() << "GDAL初始化发生未知异常，但将继续运行";
        initialized_ = false;
    }
}

// 新增：宽松的驱动验证（不会导致程序退出）
bool GDALManager::validateCriticalDriversLenient()
{
    qDebug() << "检查重要驱动可用性:";

    QStringList criticalDrivers = { "GTiff", "HFA", "JPEG", "PNG", "BMP", "ENVI" };
    QStringList availableDrivers;
    QStringList missingDrivers;

    for (const QString& driverName : criticalDrivers) {
        GDALDriverH driver = GDALGetDriverByName(driverName.toLocal8Bit().data());
        if (driver) {
            availableDrivers << driverName;
            qDebug() << QString("  %1: 可用").arg(driverName);
        }
        else {
            missingDrivers << driverName;
            qDebug() << QString("  %1: 不可用").arg(driverName);
        }
    }

    // 只要有任何一个驱动可用就认为基本可用
    bool hasAnyDriver = !availableDrivers.isEmpty();

    if (hasAnyDriver) {
        qDebug() << "可用驱动:" << availableDrivers.join(", ");
        if (!missingDrivers.isEmpty()) {
            qDebug() << "缺失驱动:" << missingDrivers.join(", ");
        }
    }
    else {
        qWarning() << "所有关键驱动都不可用，某些功能可能受限";
    }

    return hasAnyDriver;
}

void GDALManager::cleanup()
{
    QMutexLocker locker(&mutex_);

    if (initialized_) {
        qDebug() << "开始清理GDAL资源...";

        try {
            // 安全清理GDAL资源
            GDALDestroyDriverManager();
            OSRCleanup();
        }
        catch (...) {
            qDebug() << "GDAL清理过程中发生异常，忽略";
        }

        initialized_ = false;
        qDebug() << "GDAL资源清理完成";
    }
}

bool GDALManager::isInitialized()
{
    QMutexLocker locker(&mutex_);
    return initialized_;
}

void GDALManager::ensureInitialized()
{
    if (!isInitialized()) {
        initialize();
    }
}

QString GDALManager::getVersion()
{
    return QString(GDALVersionInfo("RELEASE_NAME"));
}

QStringList GDALManager::getAvailableDrivers()
{
    ensureInitialized();

    QStringList drivers;
    int count = GDALGetDriverCount();

    for (int i = 0; i < count; i++) {
        GDALDriverH driver = GDALGetDriver(i);
        if (driver) {
            QString driverName = GDALGetDriverShortName(driver);
            QString driverDesc = GDALGetDriverLongName(driver);
            drivers << QString("%1 - %2").arg(driverName).arg(driverDesc);
        }
    }

    return drivers;
}

// 新增：检查特定驱动是否可用
bool GDALManager::isDriverAvailable(const QString& driverName)
{
    ensureInitialized();
    GDALDriverH driver = GDALGetDriverByName(driverName.toLocal8Bit().data());
    return driver != nullptr;
}

// 新增：获取替代驱动
QString GDALManager::getAlternativeDriver(const QString& preferredDriver)
{
    // 驱动替代方案
    static QMap<QString, QStringList> alternatives = {
        {"GTiff", {"HFA", "ENVI", "PNG", "JPEG"}},
        {"HFA", {"GTiff", "ENVI", "PNG"}},
        {"JPEG", {"PNG", "BMP", "GTiff"}},
        {"PNG", {"JPEG", "BMP", "GTiff"}}
    };

    // 首先检查首选驱动
    if (isDriverAvailable(preferredDriver)) {
        return preferredDriver;
    }

    // 查找替代驱动
    if (alternatives.contains(preferredDriver)) {
        for (const QString& alt : alternatives[preferredDriver]) {
            if (isDriverAvailable(alt)) {
                qDebug() << QString("使用替代驱动 %1 代替 %2").arg(alt).arg(preferredDriver);
                return alt;
            }
        }
    }

    return QString(); // 没有可用的替代方案
}

void GDALManager::customErrorHandler(CPLErr eErrClass, CPLErrorNum nError, const char* pszErrorMsg)
{
    // 简化错误处理，避免过多输出
    if (eErrClass >= CE_Failure) {
        qWarning() << QString("GDAL错误(%1): %2").arg(nError).arg(QString::fromLocal8Bit(pszErrorMsg));
    }
}

// 静态便利方法的实现
namespace GDAL {
    void ensureInitialized() {
        GDALManager::ensureInitialized();
    }

    bool isInitialized() {
        return GDALManager::isInitialized();
    }

    QString getVersion() {
        return GDALManager::getVersion();
    }

    QStringList getAvailableDrivers() {
        return GDALManager::getAvailableDrivers();
    }

    bool isDriverAvailable(const QString& driverName) {
        return GDALManager::isDriverAvailable(driverName);
    }

    QString getAlternativeDriver(const QString& preferredDriver) {
        return GDALManager::getAlternativeDriver(preferredDriver);
    }
}