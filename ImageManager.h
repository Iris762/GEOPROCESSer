#ifndef IMAGEMANAGER_H
#define IMAGEMANAGER_H

#include <QObject>
#include <QStringList>
#include <QPixmap>
#include <QImage>
#include <QLabel>
#include <QDateTime>
#include <QApplication>

// 前向声明
class GDALDataset;

// 图像类型枚举
enum class ImageType {
    Unknown = 0,
    Panchromatic = 1,
    Multispectral = 2
};

// 图像信息结构体
struct ImageInfo {
    QString fileName;
    QString filePath;
    ImageType type;
    int width;
    int height;
    int channels;
    int bandCount;        // 波段数
    QDateTime importTime; // 导入时间
    QPixmap thumbnail;

    // 默认构造函数
    ImageInfo() : type(ImageType::Unknown), width(0), height(0),
        channels(0), bandCount(0) {
    }
};

class ImageManager : public QObject
{
    Q_OBJECT

public:
    explicit ImageManager(QObject* parent = nullptr);

    // 导入方法
    bool importPanchromaticImage(const QString& filePath);
    bool importMultispectralImage(const QString& filePath);
    bool importBatchImages(const QStringList& filePaths);

    // 添加已有的图像信息（用于工程加载）
    bool addImage(const ImageInfo& info);

    // 查询方法
    bool containsImage(const QString& filePath) const;
    QList<ImageInfo> getAllImages() const;
    QList<ImageInfo> getImagesByType(ImageType type) const;
    ImageInfo getImageInfo(const QString& filePath) const;

    // 图像加载和显示
    QPixmap loadImageAsPixmap(const QString& filePath, const QSize& targetSize = QSize()) const;
    QImage loadImageWithFallback(const QString& filePath) const;
    bool displayImageInLabel(const QString& filePath, QLabel* label) const;

    // 管理方法
    void removeImage(const QString& filePath);
    void clearAllImages();
    bool hasImages() const;

    // 工具方法
    QString getSupportedFormats() const;

    // 验证图像文件是否仍然存在并可访问
    bool validateImageFile(const QString& filePath) const;

signals:
    void imageAdded(const ImageInfo& info);
    void importProgress(const QString& message, int percentage);

private:
    bool importImage(const QString& filePath, ImageType type);
    bool importImageSilent(const QString& filePath, ImageType type);
    bool isValidImageFile(const QString& filePath) const;
    ImageType detectImageType(const QString& filePath) const;
    ImageInfo createImageInfo(const QString& filePath, ImageType type);
    ImageInfo createImageInfoInternal(const QString& filePath, ImageType type) const;
    QImage loadTiffWithGDAL(const QString& filePath) const;

    // 新增：安全的文件路径转换方法
    std::string safeFilePathToString(const QString& filePath) const;

    QList<ImageInfo> imageList;
};

#endif // IMAGEMANAGER_H