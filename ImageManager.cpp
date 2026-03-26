#include "ImageManager.h"
#include "GDALManager.h" 
#include <QFileInfo>
#include <QPixmap>
#include <QImageReader>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QApplication>
#include "gdal_priv.h"
#include "cpl_conv.h"

ImageManager::ImageManager(QObject* parent)
    : QObject(parent)
{
    // ✅ 使用统一的GDAL管理器
    GDAL::ensureInitialized();
}

// 新增：安全的文件路径转换方法
std::string ImageManager::safeFilePathToString(const QString& filePath) const
{
    // Qt 6 兼容的文件路径转换
    std::string result = filePath.toUtf8().toStdString();

    // 验证路径是否有效
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        // 尝试本地编码
        result = filePath.toLocal8Bit().toStdString();

        // 最后尝试Latin1
        if (!QFileInfo(QString::fromStdString(result)).exists()) {
            result = filePath.toLatin1().toStdString();
        }
    }

    return result;
}

bool ImageManager::importPanchromaticImage(const QString& filePath)
{
    return importImage(filePath, ImageType::Panchromatic);
}

bool ImageManager::importMultispectralImage(const QString& filePath)
{
    return importImage(filePath, ImageType::Multispectral);
}

bool ImageManager::importBatchImages(const QStringList& filePaths)
{
    bool success = true;
    int importedCount = 0;
    int totalFiles = filePaths.size();

    emit importProgress("开始批量导入...", 0);

    for (int i = 0; i < filePaths.size(); ++i) {
        const QString& filePath = filePaths[i];

        // 更新当前文件进度
        int baseProgress = (i * 100) / totalFiles;
        emit importProgress(QString("正在处理: %1").arg(QFileInfo(filePath).fileName()), baseProgress);

        if (!isValidImageFile(filePath)) continue;
        if (containsImage(filePath)) continue;

        ImageType type = detectImageType(filePath);
        if (importImageSilent(filePath, type)) {
            importedCount++;
        }
        else {
            success = false;
        }

        // 更新完成进度
        int completedProgress = ((i + 1) * 100) / totalFiles;
        emit importProgress(QString("已完成 %1/%2").arg(i + 1).arg(totalFiles), completedProgress);

        QApplication::processEvents();
    }

    emit importProgress(QString("批量导入完成，成功 %1 个文件").arg(importedCount), 100);
    qDebug() << "Batch import completed. Imported:" << importedCount << "files";
    return success && importedCount > 0;
}

bool ImageManager::importImage(const QString& filePath, ImageType type)
{
    if (!isValidImageFile(filePath)) {
        qDebug() << "Invalid image file:" << filePath;
        return false;
    }

    if (containsImage(filePath)) {
        qDebug() << "Image already exists:" << filePath;
        return false;
    }

    // 发送开始信号
    emit importProgress("开始导入图像...", 0);

    ImageInfo info = createImageInfo(filePath, type);
    if (info.filePath.isEmpty()) {
        emit importProgress("导入失败", 100);
        return false;
    }

    imageList.append(info);
    emit importProgress("导入完成", 100);
    emit imageAdded(info);
    return true;
}

bool ImageManager::importImageSilent(const QString& filePath, ImageType type)
{
    if (!isValidImageFile(filePath)) return false;
    if (containsImage(filePath)) return false;

    ImageInfo info = createImageInfoInternal(filePath, type);
    if (info.filePath.isEmpty()) return false;

    imageList.append(info);
    emit imageAdded(info);
    return true;
}

bool ImageManager::addImage(const ImageInfo& info)
{
    if (!validateImageFile(info.filePath)) {
        qDebug() << "Image file does not exist or is not accessible:" << info.filePath;
        return false;
    }

    if (containsImage(info.filePath)) {
        qDebug() << "Image already exists in manager:" << info.filePath;
        return false;
    }

    ImageInfo updatedInfo = info;
    QImage image = loadImageWithFallback(info.filePath);
    if (!image.isNull()) {
        updatedInfo.thumbnail = QPixmap::fromImage(image.scaled(128, 128, Qt::KeepAspectRatio));
    }

    imageList.append(updatedInfo);
    emit imageAdded(updatedInfo);

    qDebug() << "Added existing image to manager:" << info.fileName;
    return true;
}

bool ImageManager::validateImageFile(const QString& filePath) const
{
    // ✅ 在函数开头添加：
    GDAL::ensureInitialized();
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }

    // 使用改进的文件路径转换
    std::string filePathStd = safeFilePathToString(filePath);
    GDALDataset* dataset = (GDALDataset*)GDALOpen(filePathStd.c_str(), GA_ReadOnly);
    if (!dataset) {
        return false;
    }

    bool isValid = (dataset->GetRasterXSize() > 0 && dataset->GetRasterYSize() > 0);
    GDALClose(dataset);
    return isValid;
}

bool ImageManager::containsImage(const QString& filePath) const
{
    for (const auto& img : imageList) {
        if (img.filePath == filePath)
            return true;
    }
    return false;
}

QList<ImageInfo> ImageManager::getAllImages() const
{
    return imageList;
}

QList<ImageInfo> ImageManager::getImagesByType(ImageType type) const
{
    QList<ImageInfo> result;
    for (const auto& img : imageList) {
        if (img.type == type)
            result.append(img);
    }
    return result;
}

ImageInfo ImageManager::getImageInfo(const QString& filePath) const
{
    for (const auto& img : imageList) {
        if (img.filePath == filePath)
            return img;
    }
    return ImageInfo();
}

QPixmap ImageManager::loadImageAsPixmap(const QString& filePath, const QSize& targetSize) const
{
    QImage image = loadImageWithFallback(filePath);
    if (targetSize.isValid())
        image = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return QPixmap::fromImage(image);
}

QImage ImageManager::loadImageWithFallback(const QString& filePath) const
{
    QImageReader reader(filePath);
    QImage image = reader.read();

    if (image.isNull() && filePath.endsWith(".tif", Qt::CaseInsensitive)) {
        image = loadTiffWithGDAL(filePath);
    }

    return image;
}

QImage ImageManager::loadTiffWithGDAL(const QString& filePath) const
{
    // ✅ 在函数开头添加：
    GDAL::ensureInitialized();
    // 使用改进的文件路径转换
    std::string filePathStd = safeFilePathToString(filePath);
    GDALDataset* dataset = (GDALDataset*)GDALOpen(filePathStd.c_str(), GA_ReadOnly);
    if (!dataset) {
        qDebug() << "GDAL无法打开文件:" << filePath;
        return QImage();
    }

    int width = dataset->GetRasterXSize();
    int height = dataset->GetRasterYSize();
    int bands = dataset->GetRasterCount();

    qDebug() << QString("GDAL加载TIFF - 尺寸: %1x%2, 波段数: %3").arg(width).arg(height).arg(bands);

    if (bands <= 0 || bands > 4 || width <= 0 || height <= 0) {
        qDebug() << "TIFF文件参数无效";
        GDALClose(dataset);
        return QImage();
    }

    // 对于大图像，创建缩略图
    const int maxSize = 2048; // 最大尺寸限制
    int displayWidth = width;
    int displayHeight = height;

    if (width > maxSize || height > maxSize) {
        double scale = static_cast<double>(maxSize) / std::max(width, height);
        displayWidth = static_cast<int>(width * scale);
        displayHeight = static_cast<int>(height * scale);
        qDebug() << QString("创建缩略图: %1x%2 -> %3x%4").arg(width).arg(height).arg(displayWidth).arg(displayHeight);
    }

    QVector<uchar*> data(bands);
    bool success = true;

    try {
        for (int i = 0; i < bands; ++i) {
            data[i] = new uchar[displayWidth * displayHeight];

            // 使用GDAL的重采样功能
            CPLErr result = dataset->GetRasterBand(i + 1)->RasterIO(
                GF_Read, 0, 0, width, height,
                data[i], displayWidth, displayHeight, GDT_Byte, 0, 0);

            if (result != CE_None) {
                qDebug() << QString("读取波段%1失败: %2").arg(i + 1).arg(CPLGetLastErrorMsg());
                success = false;
                break;
            }
        }

        if (!success) {
            for (int i = 0; i < bands; ++i) {
                if (data[i]) delete[] data[i];
            }
            GDALClose(dataset);
            return QImage();
        }

        QImage image(displayWidth, displayHeight, bands == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);

        for (int y = 0; y < displayHeight; ++y) {
            uchar* line = image.scanLine(y);
            for (int x = 0; x < displayWidth; ++x) {
                int idx = y * displayWidth + x;
                if (bands == 1) {
                    line[x] = data[0][idx];
                }
                else if (bands >= 3) {
                    line[x * 3 + 0] = data[0][idx]; // Red
                    line[x * 3 + 1] = data[1][idx]; // Green
                    line[x * 3 + 2] = data[2][idx]; // Blue
                }
            }
        }

        for (int i = 0; i < bands; ++i) {
            delete[] data[i];
        }
        GDALClose(dataset);

        qDebug() << QString("GDAL TIFF加载成功: %1x%2").arg(displayWidth).arg(displayHeight);
        return image;
    }
    catch (const std::exception& e) {
        qDebug() << "GDAL TIFF加载异常:" << e.what();
        for (int i = 0; i < bands; ++i) {
            if (data[i]) delete[] data[i];
        }
        GDALClose(dataset);
        return QImage();
    }
}

bool ImageManager::displayImageInLabel(const QString& filePath, QLabel* label) const
{
    if (!label) return false;
    QPixmap pixmap = loadImageAsPixmap(filePath, label->size());
    if (pixmap.isNull()) {
        label->setText("无法加载图像");
        return false;
    }
    label->setPixmap(pixmap);
    label->setAlignment(Qt::AlignCenter);
    return true;
}

void ImageManager::removeImage(const QString& filePath)
{
    for (int i = 0; i < imageList.size(); ++i) {
        if (imageList[i].filePath == filePath) {
            imageList.removeAt(i);
            break;
        }
    }
}

void ImageManager::clearAllImages()
{
    imageList.clear();
}

bool ImageManager::hasImages() const
{
    return !imageList.isEmpty();
}

QString ImageManager::getSupportedFormats() const
{
    return "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif)";
}

bool ImageManager::isValidImageFile(const QString& filePath) const
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) return false;
    QString ext = fileInfo.suffix().toLower();
    return QStringList({ "png", "jpg", "jpeg", "bmp", "tif", "tiff", "gif" }).contains(ext);
}

ImageType ImageManager::detectImageType(const QString& filePath) const
{
    QString name = QFileInfo(filePath).fileName().toLower();
    if (name.contains("pan")) return ImageType::Panchromatic;
    if (name.contains("multi") || name.contains("ms")) return ImageType::Multispectral;
    return ImageType::Unknown;
}

// 内部 const 版本，不发送进度信号
ImageInfo ImageManager::createImageInfoInternal(const QString& filePath, ImageType type) const
{
    // ✅ 在函数开头添加：
    GDAL::ensureInitialized();
    ImageInfo info;

    // 首先尝试用Qt的图像读取器
    QImage image = loadImageWithFallback(filePath);
    if (image.isNull()) {
        qDebug() << "无法读取图像文件:" << filePath;
        return info;
    }

    QFileInfo fi(filePath);
    info.filePath = filePath;
    info.fileName = fi.fileName();
    info.type = type;
    info.width = image.width();
    info.height = image.height();
    info.channels = image.depth() / 8;
    info.importTime = QDateTime::currentDateTime();
    info.thumbnail = QPixmap::fromImage(image.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // 使用GDAL获取准确的波段数
    std::string filePathStd = safeFilePathToString(filePath);
    GDALDataset* dataset = (GDALDataset*)GDALOpen(filePathStd.c_str(), GA_ReadOnly);
    if (dataset) {
        info.bandCount = dataset->GetRasterCount();
        GDALClose(dataset);
        qDebug() << QString("GDAL检测到 %1 个波段").arg(info.bandCount);
    }
    else {
        // 如果GDAL无法打开，根据图像格式推测波段数
        if (image.format() == QImage::Format_Grayscale8) {
            info.bandCount = 1;
        }
        else if (image.format() == QImage::Format_RGB888 || image.format() == QImage::Format_RGB32) {
            info.bandCount = 3;
        }
        else if (image.format() == QImage::Format_ARGB32) {
            info.bandCount = 4;
        }
        else {
            info.bandCount = 3; // 默认值
        }
        qDebug() << QString("根据Qt格式推测 %1 个波段").arg(info.bandCount);
    }

    qDebug() << QString("创建图像信息: %1 (%2x%3, %4波段)")
        .arg(info.fileName).arg(info.width).arg(info.height).arg(info.bandCount);

    return info;
}

// 带进度显示的版本（非const）
ImageInfo ImageManager::createImageInfo(const QString& filePath, ImageType type)
{
    emit importProgress("验证文件格式...", 10);
    QApplication::processEvents();

    emit importProgress("加载图像数据...", 30);

    ImageInfo info = createImageInfoInternal(filePath, type);
    if (info.filePath.isEmpty()) {
        emit importProgress("无法读取图像文件", 100);
        return info;
    }

    emit importProgress("读取图像信息...", 60);
    emit importProgress("生成缩略图...", 80);
    emit importProgress("获取波段信息...", 90);

    return info;
}