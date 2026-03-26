#ifndef GDALMANAGER_H
#define GDALMANAGER_H

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <QString>
#include <QStringList>
#include <QMutex>

class GDALManager
{
public:
    // 单例访问方法
    static void initialize();
    static void cleanup();
    static bool isInitialized();
    static void ensureInitialized();

    // 信息查询方法
    static QString getVersion();
    static QStringList getAvailableDrivers();

    // 新增：驱动检查和替代方案
    static bool isDriverAvailable(const QString& driverName);
    static QString getAlternativeDriver(const QString& preferredDriver);

private:
    GDALManager();
    ~GDALManager();

    // 禁止拷贝和赋值
    GDALManager(const GDALManager&) = delete;
    GDALManager& operator=(const GDALManager&) = delete;

    // 内部方法
    static bool validateCriticalDriversLenient();
    static void customErrorHandler(CPLErr eErrClass, CPLErrorNum nError, const char* pszErrorMsg);

    // 静态成员
    static bool initialized_;
    static QMutex mutex_;
};

// 便利的命名空间接口
namespace GDAL {
    void ensureInitialized();
    bool isInitialized();
    QString getVersion();
    QStringList getAvailableDrivers();
    bool isDriverAvailable(const QString& driverName);
    QString getAlternativeDriver(const QString& preferredDriver);
}

#endif // GDALMANAGER_H