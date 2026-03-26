#ifndef IMAGEDISPLAYWIDGET_H
#define IMAGEDISPLAYWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPushButton>
#include <QTimer>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QMap>
#include <QPair>
#include <QScrollBar>
#include <QSet>
#include <QList>
#include <QLabel>
#include <QPainter>
#include <QCursor>
#include <algorithm>

#include "gdal_priv.h"
#include "cpl_conv.h"
#include "ControlPointManager.h"  // 包含控制点管理器头文件

// 前向声明
class ImageDisplayWidget;

// 加载模式枚举
enum class LoadingMode {
    Direct,    // 直接加载模式（小图像）
    Tiled      // 瓦片加载模式（大图像）
};

class TiledGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit TiledGraphicsView(ImageDisplayWidget* parent = nullptr);

    // 控制点管理相关方法
    void setControlPointManager(ControlPointManager* manager) { controlPointManager_ = manager; }
    void setControlPointMode(bool enabled) { controlPointMode_ = enabled; updateCursor(); }

protected:
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateCursor();
    QCursor createCrosshairCursor();

    ImageDisplayWidget* displayWidget_;
    ControlPointManager* controlPointManager_;
    bool controlPointMode_;

    // 手动拖拽支持
    bool isDragging_;
    QPoint lastPanPoint_;
};

class ImageDisplayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ImageDisplayWidget(QWidget* parent = nullptr);
    ~ImageDisplayWidget();

    // 图像显示和管理
    void displayImage(const QString& filePath);
    void clearDisplay();

    // 缩放操作
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToActualSize();
    void setZoomLevel(double level);
    double getCurrentZoomLevel() const;

    // 图像信息获取
    QSize getImageSize() const;
    bool hasImage() const;
    QString getCurrentImagePath() const { return currentImagePath_; }
    LoadingMode getCurrentLoadingMode() const { return currentLoadingMode_; }

    // 工具栏控制
    void setToolBarVisible(bool visible);

    // 控制点功能
    void setControlPointManager(ControlPointManager* manager);
    void setControlPointMode(bool enabled);
    ControlPointManager* getControlPointManager() const { return controlPointManager_; }
    void updateControlPointDisplay();

    // 图像类型管理
    void setImageType(ControlPointType type) { imageType_ = type; }
    ControlPointType getImageType() const { return imageType_; }

    // 缩放常量
    static const double MIN_ZOOM;
    static const double MAX_ZOOM;
    static const double ZOOM_STEP;

    // 大小阈值常量（用于判断使用哪种加载方式）
    static const qint64 LARGE_IMAGE_THRESHOLD_BYTES = 2LL * 1024 * 1024 * 1024; // 2GB

signals:
    void imageDisplayed(const QString& filePath, int width, int height, int bandCount);
    void imageDisplayFailed(const QString& errorMessage);
    void zoomLevelChanged(double level);
    void progressUpdate(const QString& message, int value);
    void progressFinished();
    void tileLoadProgress(int loaded, int total);

    // 控制点相关信号
    void controlPointClicked(int pointId);
    void sceneClicked(const QPointF& scenePos);

public slots:
    void onViewScrolled();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void onZoomInClicked();
    void onZoomOutClicked();
    void onZoomFitClicked();
    void onZoomActualClicked();
    void onLoadTilesTimer();

private:
    // UI组件
    QVBoxLayout* mainLayout_;
    QHBoxLayout* toolBarLayout_;
    QWidget* toolBarWidget_;
    TiledGraphicsView* imageView_;
    QGraphicsScene* imageScene_;
    QPushButton* zoomInBtn_;
    QPushButton* zoomOutBtn_;
    QPushButton* zoomFitBtn_;
    QPushButton* zoomActualBtn_;

    // GDAL相关
    GDALDataset* gdalDataset_;
    QString currentImagePath_;

    // 图像信息
    int imageWidth_;
    int imageHeight_;
    int tileSize_;
    int tilesX_;
    int tilesY_;
    int bandCount_;
    GDALDataType dataType_;

    // 加载模式
    LoadingMode currentLoadingMode_;

    // 数据范围
    double globalMin_;
    double globalMax_;
    bool hasGlobalRange_;

    // 瓦片缓存（仅瓦片模式使用）
    QMap<QPair<int, int>, QGraphicsPixmapItem*> tileCache_;

    // 延迟加载控制（仅瓦片模式使用）
    QTimer* loadTilesTimer_;
    bool needsReload_;

    // 并发控制（仅瓦片模式使用）
    int maxConcurrentTiles_;
    int currentLoadingTiles_;

    // 控制点相关成员
    ControlPointManager* controlPointManager_;
    ControlPointType imageType_;

    // 私有方法 - UI初始化
    void setupUI();
    void setupImageView();
    void setupToolBar();
    void setupConnections();
    void applyStyles();

    // 私有方法 - 加载模式选择
    LoadingMode determineLoadingMode(const QString& filePath);
    bool shouldUseTiledLoading(qint64 fileSize, int width, int height);

    // 私有方法 - 直接加载（小图像）
    bool loadImageDirectly(const QString& filePath);
    bool loadByteDataDirect(QImage& image);
    bool load16BitDataDirect(QImage& image);
    bool load32BitDataDirect(QImage& image);
    bool loadFloatDataDirect(QImage& image);

    // 私有方法 - 瓦片加载（大图像）
    bool loadImageWithTiles(const QString& filePath);
    void calculateGlobalDataRange();
    bool loadTile(int x, int y);
    void loadVisibleTiles();
    void loadVisibleTilesDelayed();
    void clearTileCache();
    void updateLoadTimerInterval();

    // 私有方法 - 辅助功能
    void updateZoomButtons();
    void emitProgress(const QString& message, int value);
    void cleanupGDALResources();
    double getCurrentTransformScale() const;
    std::string safeFilePathToString(const QString& filePath) const;
};

#endif // IMAGEDISPLAYWIDGET_H