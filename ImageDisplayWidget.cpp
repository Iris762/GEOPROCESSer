#include "ImageDisplayWidget.h"
#include "GDALManager.h"  
#include <QApplication>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QProgressBar>
#include <QScrollBar>
#include <algorithm>
#include <cmath>
#include <QThread>
#include <QPainter>
#include <QMouseEvent>

// TiledGraphicsView 实现
TiledGraphicsView::TiledGraphicsView(ImageDisplayWidget* parent)
    : QGraphicsView(parent)
    , displayWidget_(parent)
    , controlPointManager_(nullptr)
    , controlPointMode_(false)
    , isDragging_(false)
{
    updateCursor();
}

void TiledGraphicsView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    if (displayWidget_) {
        displayWidget_->onViewScrolled();
    }
}

void TiledGraphicsView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (displayWidget_) {
        displayWidget_->onViewScrolled();
    }
}

void TiledGraphicsView::mousePressEvent(QMouseEvent* event)
{
    // 控制点模式处理
    if (controlPointMode_ && controlPointManager_ && event->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(event->pos());

        if (scene() && scene()->sceneRect().contains(scenePos)) {
            ControlPointType imageType = displayWidget_->getImageType();
            int pointId = controlPointManager_->findControlPointAt(scenePos, imageType, 15.0);

            if (pointId == -1) {
                qDebug() << QString("精确坐标: X=%1, Y=%2").arg(scenePos.x(), 0, 'f', 6).arg(scenePos.y(), 0, 'f', 6);
                qDebug() << QString("在%1图像位置添加控制点:").arg(
                    imageType == ControlPointType::Reference ? "参考" : "待配准") << scenePos;
                controlPointManager_->addControlPoint(scenePos, imageType);
                event->accept();
                return;
            }
        }
    }

    // 在控制点模式下，右键或中键用于拖拽
    if (controlPointMode_ && (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)) {
        isDragging_ = true;
        lastPanPoint_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // 非控制点模式，正常处理
    if (!controlPointMode_) {
        QGraphicsView::mousePressEvent(event);
    }
}

void TiledGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
    // 控制点模式下的手动拖拽
    if (controlPointMode_ && isDragging_) {
        QPointF delta = event->pos() - lastPanPoint_;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        lastPanPoint_ = event->pos();
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void TiledGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    if (controlPointMode_ && isDragging_) {
        isDragging_ = false;
        setCursor(createCrosshairCursor());
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void TiledGraphicsView::updateCursor()
{
    if (controlPointMode_) {
        setDragMode(QGraphicsView::NoDrag);
        setCursor(createCrosshairCursor());
        qDebug() << "设置控制点模式光标";
    }
    else {
        setDragMode(QGraphicsView::ScrollHandDrag);
        setCursor(Qt::ArrowCursor);
        qDebug() << "设置正常模式光标";
    }
}

QCursor TiledGraphicsView::createCrosshairCursor()
{
    QPixmap cursorPixmap(41, 41);
    cursorPixmap.fill(Qt::transparent);

    QPainter painter(&cursorPixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(QColor(255, 0, 0, 220), 0.3);
    painter.setPen(pen);

    int center = 15;
    int longSize = 12;
    int shortSize = 4;

    // 绘制十字丝
    painter.drawLine(center, center - longSize, center, center - shortSize);
    painter.drawLine(center, center + shortSize, center, center + longSize);
    painter.drawLine(center - longSize, center, center - shortSize, center);
    painter.drawLine(center + shortSize, center, center + longSize, center);

    // 绘制中心点
    painter.setBrush(QColor(255, 0, 0, 255));
    painter.setPen(QColor(255, 0, 0, 255));
    painter.drawEllipse(center - 1, center - 1, 3, 3);

    // 添加外圈
    QPen outerPen(QColor(255, 255, 255, 180), 2.5);
    painter.setPen(outerPen);
    painter.setBrush(Qt::NoBrush);

    painter.drawLine(center, center - longSize, center, center - shortSize);
    painter.drawLine(center, center + shortSize, center, center + longSize);
    painter.drawLine(center - longSize, center, center - shortSize, center);
    painter.drawLine(center + shortSize, center, center + longSize, center);

    return QCursor(cursorPixmap, center, center);
}

// ImageDisplayWidget 常量定义
const double ImageDisplayWidget::MIN_ZOOM = 0.01;
const double ImageDisplayWidget::MAX_ZOOM = 10.0;
const double ImageDisplayWidget::ZOOM_STEP = 0.1;

// ImageDisplayWidget 构造函数
ImageDisplayWidget::ImageDisplayWidget(QWidget* parent)
    : QWidget(parent)
    , mainLayout_(nullptr)
    , toolBarLayout_(nullptr)
    , toolBarWidget_(nullptr)
    , imageView_(nullptr)
    , imageScene_(nullptr)
    , zoomInBtn_(nullptr)
    , zoomOutBtn_(nullptr)
    , zoomFitBtn_(nullptr)
    , zoomActualBtn_(nullptr)
    , gdalDataset_(nullptr)
    , currentImagePath_("")
    , imageWidth_(0)
    , imageHeight_(0)
    , tileSize_(512)
    , tilesX_(0)
    , tilesY_(0)
    , bandCount_(0)
    , dataType_(GDT_Unknown)
    , currentLoadingMode_(LoadingMode::Direct)
    , globalMin_(0.0)
    , globalMax_(255.0)
    , hasGlobalRange_(false)
    , loadTilesTimer_(nullptr)
    , needsReload_(false)
    , maxConcurrentTiles_(8)
    , currentLoadingTiles_(0)
    , controlPointManager_(nullptr)
    , imageType_(ControlPointType::Reference)
{
    // ✅ 使用统一的GDAL管理器
    GDAL::ensureInitialized();

    // 创建防抖定时器
    loadTilesTimer_ = new QTimer(this);
    loadTilesTimer_->setSingleShot(true);
    loadTilesTimer_->setInterval(100);

    // 初始化UI
    setupUI();
    setupImageView();
    setupToolBar();
    setupConnections();
    applyStyles();

    // 初始状态
    clearDisplay();
}

ImageDisplayWidget::~ImageDisplayWidget()
{
    qDebug() << "开始销毁 ImageDisplayWidget";

    // 停止所有定时器
    if (loadTilesTimer_) {
        loadTilesTimer_->stop();
        loadTilesTimer_->deleteLater();
        loadTilesTimer_ = nullptr;
    }

    // 断开所有信号连接
    if (controlPointManager_) {
        disconnect(controlPointManager_, nullptr, this, nullptr);
        controlPointManager_ = nullptr;
    }

    // 安全清理显示内容
    try {
        clearDisplay();
    }
    catch (...) {
        qDebug() << "销毁时清理显示内容发生异常";
    }

    // 清理GDAL资源
    cleanupGDALResources();

    qDebug() << "ImageDisplayWidget 销毁完成";
}

void ImageDisplayWidget::setupUI()
{
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(5, 5, 5, 5);
    mainLayout_->setSpacing(5);

    // 创建工具栏容器
    toolBarWidget_ = new QWidget(this);
    toolBarLayout_ = new QHBoxLayout(toolBarWidget_);
    toolBarLayout_->setContentsMargins(5, 5, 5, 5);
    toolBarLayout_->setSpacing(10);

    // 创建图形视图
    imageView_ = new TiledGraphicsView(this);
    imageScene_ = new QGraphicsScene(this);

    // 添加到主布局
    mainLayout_->addWidget(toolBarWidget_, 0);
    mainLayout_->addWidget(imageView_, 1);
}

void ImageDisplayWidget::setupImageView()
{
    imageView_->setScene(imageScene_);
    imageView_->setAlignment(Qt::AlignCenter);
    imageView_->setRenderHint(QPainter::SmoothPixmapTransform, false);
    imageView_->setDragMode(QGraphicsView::ScrollHandDrag);
    imageView_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    imageView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    imageView_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 优化设置
    imageView_->setOptimizationFlags(QGraphicsView::DontSavePainterState | QGraphicsView::DontAdjustForAntialiasing);
    imageView_->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    imageView_->setCacheMode(QGraphicsView::CacheNone);
    imageView_->setRenderHint(QPainter::Antialiasing, false);

    // 设置场景
    if (imageScene_) {
        imageScene_->setItemIndexMethod(QGraphicsScene::NoIndex);
        imageScene_->setSceneRect(-50000, -50000, 100000, 100000);
    }
}

void ImageDisplayWidget::setupToolBar()
{
    // 创建缩放按钮
    zoomInBtn_ = new QPushButton("放大", toolBarWidget_);
    zoomOutBtn_ = new QPushButton("缩小", toolBarWidget_);
    zoomFitBtn_ = new QPushButton("自适应窗口", toolBarWidget_);
    zoomActualBtn_ = new QPushButton("实际大小", toolBarWidget_);

    // 设置按钮大小
    QSize buttonSize(80, 30);
    zoomInBtn_->setFixedSize(buttonSize);
    zoomOutBtn_->setFixedSize(buttonSize);
    zoomFitBtn_->setFixedSize(buttonSize);
    zoomActualBtn_->setFixedSize(buttonSize);

    // 添加到工具栏布局
    toolBarLayout_->addWidget(zoomInBtn_);
    toolBarLayout_->addWidget(zoomOutBtn_);
    toolBarLayout_->addWidget(zoomFitBtn_);
    toolBarLayout_->addWidget(zoomActualBtn_);
    toolBarLayout_->addStretch();
}

void ImageDisplayWidget::setupConnections()
{
    // 连接缩放按钮
    connect(zoomInBtn_, &QPushButton::clicked, this, &ImageDisplayWidget::onZoomInClicked);
    connect(zoomOutBtn_, &QPushButton::clicked, this, &ImageDisplayWidget::onZoomOutClicked);
    connect(zoomFitBtn_, &QPushButton::clicked, this, &ImageDisplayWidget::onZoomFitClicked);
    connect(zoomActualBtn_, &QPushButton::clicked, this, &ImageDisplayWidget::onZoomActualClicked);

    // 连接防抖定时器
    connect(loadTilesTimer_, &QTimer::timeout, this, &ImageDisplayWidget::onLoadTilesTimer);
}

void ImageDisplayWidget::applyStyles()
{
    QString buttonStyle =
        "QPushButton {"
        "    padding: 5px 10px;"
        "    border: 1px solid #ccc;"
        "    border-radius: 3px;"
        "    background-color: #f5f5f5;"
        "}"
        "QPushButton:hover {"
        "    background-color: #e5e5e5;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #d5d5d5;"
        "}"
        "QPushButton:disabled {"
        "    color: #999;"
        "    background-color: #f0f0f0;"
        "}";

    zoomInBtn_->setStyleSheet(buttonStyle);
    zoomOutBtn_->setStyleSheet(buttonStyle);
    zoomFitBtn_->setStyleSheet(buttonStyle);
    zoomActualBtn_->setStyleSheet(buttonStyle);

    // 设置图形视图样式
    imageView_->setStyleSheet(
        "QGraphicsView {"
        "    background-color: #f8f8f8;"
        "    border: 1px solid #ddd;"
        "}"
    );
}

std::string ImageDisplayWidget::safeFilePathToString(const QString& filePath) const
{
    std::string result = filePath.toUtf8().toStdString();

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        result = filePath.toLocal8Bit().toStdString();
        if (!QFileInfo(QString::fromStdString(result)).exists()) {
            result = filePath.toLatin1().toStdString();
        }
    }

    return result;
}

bool ImageDisplayWidget::hasImage() const
{
    return gdalDataset_ != nullptr && imageWidth_ > 0 && imageHeight_ > 0;
}

LoadingMode ImageDisplayWidget::determineLoadingMode(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    qint64 fileSize = fileInfo.size();

    // 只检查文件大小：超过2GB使用瓦片加载
    if (fileSize > LARGE_IMAGE_THRESHOLD_BYTES) {
        qDebug() << QString("文件大小 %1 GB超过2GB阈值，使用瓦片加载").arg(fileSize / (1024.0 * 1024.0 * 1024.0));
        return LoadingMode::Tiled;
    }

    qDebug() << QString("文件大小 %1 MB，使用直接加载").arg(fileSize / (1024.0 * 1024.0));
    return LoadingMode::Direct;
}

bool ImageDisplayWidget::shouldUseTiledLoading(qint64 fileSize, int width, int height)
{
    Q_UNUSED(width)
        Q_UNUSED(height)

        // 只根据文件大小判断：超过2GB使用瓦片加载
        return fileSize > LARGE_IMAGE_THRESHOLD_BYTES;
}

void ImageDisplayWidget::displayImage(const QString& filePath)
{
    if (filePath.isEmpty()) {
        clearDisplay();
        return;
    }

    qDebug() << "开始智能加载图像:" << filePath;

    // 文件检查
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        emit progressFinished();
        clearDisplay();
        emit imageDisplayFailed("文件不存在: " + filePath);
        return;
    }

    if (!fileInfo.isReadable()) {
        emit progressFinished();
        clearDisplay();
        emit imageDisplayFailed("文件无法读取: " + filePath);
        return;
    }

    emitProgress("分析图像特征...", 5);

    // 确定加载模式
    currentLoadingMode_ = determineLoadingMode(filePath);

    emitProgress("开始加载图像...", 10);

    bool loadSuccess = false;
    if (currentLoadingMode_ == LoadingMode::Direct) {
        qDebug() << "使用直接加载模式";
        loadSuccess = loadImageDirectly(filePath);
    }
    else {
        qDebug() << "使用瓦片加载模式";
        loadSuccess = loadImageWithTiles(filePath);
    }

    if (loadSuccess) {
        emitProgress("图像加载完成", 100);
        if (currentLoadingMode_ == LoadingMode::Tiled) {
            emit tileLoadProgress(1, 1);
        }

        QTimer::singleShot(200, this, [this, filePath]() {
            emit progressFinished();
            emit imageDisplayed(filePath, imageWidth_, imageHeight_, bandCount_);
            });

        QApplication::processEvents();
        QTimer::singleShot(400, this, [this]() {
            zoomToFit();
            QTimer::singleShot(200, this, [this]() {
                updateControlPointDisplay();
                });
            });
    }
    else {
        emit progressFinished();
        emit imageDisplayFailed("无法加载图像文件: " + filePath);
    }

    updateZoomButtons();
}

bool ImageDisplayWidget::loadImageDirectly(const QString& filePath)
{
    // 确保GDAL已初始化
    GDAL::ensureInitialized();
    // 关闭之前的数据集
    if (gdalDataset_) {
        GDALClose(gdalDataset_);
        gdalDataset_ = nullptr;
    }

    emitProgress("正在打开图像文件...", 30);

    // 打开GDAL数据集
    std::string filePathStd = safeFilePathToString(filePath);
    gdalDataset_ = (GDALDataset*)GDALOpen(filePathStd.c_str(), GA_ReadOnly);

    if (!gdalDataset_) {
        qDebug() << "GDAL无法打开文件:" << filePath;
        return false;
    }

    // 获取图像信息
    imageWidth_ = gdalDataset_->GetRasterXSize();
    imageHeight_ = gdalDataset_->GetRasterYSize();
    bandCount_ = gdalDataset_->GetRasterCount();

    if (bandCount_ > 0) {
        dataType_ = gdalDataset_->GetRasterBand(1)->GetRasterDataType();
    }

    qDebug() << QString("直接加载图像信息: %1x%2, %3波段, 类型:%4")
        .arg(imageWidth_).arg(imageHeight_).arg(bandCount_).arg(dataType_);

    if (imageWidth_ <= 0 || imageHeight_ <= 0 || bandCount_ <= 0) {
        qDebug() << "图像参数无效";
        return false;
    }

    // 清空瓦片设置
    tilesX_ = 0;
    tilesY_ = 0;
    clearTileCache();
    hasGlobalRange_ = false;

    imageScene_->clear();
    emitProgress("正在加载图像数据...", 40);

    // 创建QImage
    QImage image(imageWidth_, imageHeight_, QImage::Format_RGB888);
    image.fill(Qt::black);

    bool loadSuccess = false;
    try {
        if (dataType_ == GDT_Byte) {
            loadSuccess = loadByteDataDirect(image);
        }
        else if (dataType_ == GDT_UInt16 || dataType_ == GDT_Int16) {
            loadSuccess = load16BitDataDirect(image);
        }
        else if (dataType_ == GDT_UInt32 || dataType_ == GDT_Int32) {
            loadSuccess = load32BitDataDirect(image);
        }
        else if (dataType_ == GDT_Float32 || dataType_ == GDT_Float64) {
            loadSuccess = loadFloatDataDirect(image);
        }
        else {
            loadSuccess = load16BitDataDirect(image);
        }
    }
    catch (const std::exception& e) {
        qDebug() << "图像加载异常:" << e.what();
        return false;
    }

    if (loadSuccess && !image.isNull()) {
        emitProgress("创建显示...", 95);

        imageScene_->clear();
        imageScene_->setSceneRect(0, 0, imageWidth_, imageHeight_);

        QGraphicsPixmapItem* pixmapItem = imageScene_->addPixmap(QPixmap::fromImage(image));
        pixmapItem->setPos(0, 0);

        currentImagePath_ = filePath;
        return true;
    }

    return false;
}

bool ImageDisplayWidget::loadImageWithTiles(const QString& filePath)
{
    // ✅ 在函数开头添加：
    GDAL::ensureInitialized();
    // 关闭之前的数据集
    if (gdalDataset_) {
        GDALClose(gdalDataset_);
        gdalDataset_ = nullptr;
    }

    emitProgress("正在打开大图像文件...", 30);

    // 打开GDAL数据集
    std::string filePathStd = safeFilePathToString(filePath);
    gdalDataset_ = (GDALDataset*)GDALOpen(filePathStd.c_str(), GA_ReadOnly);

    if (!gdalDataset_) {
        qDebug() << "GDAL无法打开文件:" << filePath;
        return false;
    }

    emitProgress("正在读取图像信息...", 50);

    // 获取图像尺寸
    imageWidth_ = gdalDataset_->GetRasterXSize();
    imageHeight_ = gdalDataset_->GetRasterYSize();
    bandCount_ = gdalDataset_->GetRasterCount();

    if (bandCount_ > 0) {
        dataType_ = gdalDataset_->GetRasterBand(1)->GetRasterDataType();
    }

    // 计算需要的瓦片数量
    tilesX_ = (imageWidth_ + tileSize_ - 1) / tileSize_;
    tilesY_ = (imageHeight_ + tileSize_ - 1) / tileSize_;

    qDebug() << QString("瓦片加载图像信息 - 尺寸: %1x%2, 波段数: %3, 瓦片: %4x%5")
        .arg(imageWidth_).arg(imageHeight_).arg(bandCount_).arg(tilesX_).arg(tilesY_);

    // 设置场景大小
    imageScene_->clear();
    imageScene_->setSceneRect(0, 0, imageWidth_, imageHeight_);

    // 清空瓦片缓存
    clearTileCache();

    // 保存当前图像路径
    currentImagePath_ = filePath;

    emitProgress("正在计算数据范围...", 70);

    // 计算全局数据范围
    calculateGlobalDataRange();

    emitProgress("正在初始加载可见瓦片...", 90);

    // 动态调整防抖定时器间隔
    updateLoadTimerInterval();

    // 大图优化：减少初始加载数量
    int imageSize = imageWidth_ * imageHeight_;
    int initialRange;

    if (imageSize > 100000000) {
        initialRange = 0; // 只加载中心1个瓦片
    }
    else if (imageSize > 25000000) {
        initialRange = 1; // 加载中心3x3区域
    }
    else {
        initialRange = 2; // 加载中心5x5区域
    }

    // 立即加载中心区域的瓦片
    int centerX = tilesX_ / 2;
    int centerY = tilesY_ / 2;

    int loadedCount = 0;
    for (int x = std::max(0, centerX - initialRange); x <= std::min(tilesX_ - 1, centerX + initialRange); ++x) {
        for (int y = std::max(0, centerY - initialRange); y <= std::min(tilesY_ - 1, centerY + initialRange); ++y) {
            if (loadTile(x, y)) {
                loadedCount++;
            }

            if (loadedCount % 2 == 0) {
                QApplication::processEvents();
            }

            if (imageSize > 100000000 && loadedCount >= 1) {
                break;
            }
        }
        if (imageSize > 100000000 && loadedCount >= 1) {
            break;
        }
    }

    qDebug() << QString("初始加载完成，缓存瓦片数: %1").arg(tileCache_.size());
    return true;
}

bool ImageDisplayWidget::loadByteDataDirect(QImage& image)
{
    if (bandCount_ == 1) {
        emitProgress("加载单波段8位数据...", 50);
        std::vector<unsigned char> buffer(static_cast<size_t>(imageWidth_) * imageHeight_);
        CPLErr result = gdalDataset_->GetRasterBand(1)->RasterIO(
            GF_Read, 0, 0, imageWidth_, imageHeight_,
            buffer.data(), imageWidth_, imageHeight_, GDT_Byte, 0, 0);

        if (result != CE_None) {
            qDebug() << "读取单波段失败";
            return false;
        }

        for (int y = 0; y < imageHeight_; ++y) {
            unsigned char* line = image.scanLine(y);
            for (int x = 0; x < imageWidth_; ++x) {
                unsigned char value = buffer[static_cast<size_t>(y) * imageWidth_ + x];
                line[x * 3 + 0] = value;
                line[x * 3 + 1] = value;
                line[x * 3 + 2] = value;
            }
            if (y % (imageHeight_ / 20) == 0) {
                int progress = 50 + (y * 40) / imageHeight_;
                emitProgress("处理单波段数据...", progress);
                QApplication::processEvents();
            }
        }
        return true;
    }
    else if (bandCount_ >= 3) {
        emitProgress("加载多波段8位数据...", 50);
        std::vector<unsigned char> redBuffer(static_cast<size_t>(imageWidth_) * imageHeight_);
        std::vector<unsigned char> greenBuffer(static_cast<size_t>(imageWidth_) * imageHeight_);
        std::vector<unsigned char> blueBuffer(static_cast<size_t>(imageWidth_) * imageHeight_);

        if (gdalDataset_->GetRasterBand(1)->RasterIO(
            GF_Read, 0, 0, imageWidth_, imageHeight_,
            redBuffer.data(), imageWidth_, imageHeight_, GDT_Byte, 0, 0) != CE_None ||
            gdalDataset_->GetRasterBand(2)->RasterIO(
                GF_Read, 0, 0, imageWidth_, imageHeight_,
                greenBuffer.data(), imageWidth_, imageHeight_, GDT_Byte, 0, 0) != CE_None ||
            gdalDataset_->GetRasterBand(3)->RasterIO(
                GF_Read, 0, 0, imageWidth_, imageHeight_,
                blueBuffer.data(), imageWidth_, imageHeight_, GDT_Byte, 0, 0) != CE_None) {
            qDebug() << "读取RGB波段失败";
            return false;
        }

        emitProgress("合成RGB图像...", 85);
        for (int y = 0; y < imageHeight_; ++y) {
            unsigned char* line = image.scanLine(y);
            for (int x = 0; x < imageWidth_; ++x) {
                size_t idx = static_cast<size_t>(y) * imageWidth_ + x;
                line[x * 3 + 0] = redBuffer[idx];
                line[x * 3 + 1] = greenBuffer[idx];
                line[x * 3 + 2] = blueBuffer[idx];
            }
            if (y % (imageHeight_ / 10) == 0) {
                int progress = 85 + (y * 10) / imageHeight_;
                emitProgress("合成RGB图像...", progress);
                QApplication::processEvents();
            }
        }
        return true;
    }
    return false;
}

bool ImageDisplayWidget::load16BitDataDirect(QImage& image)
{
    qDebug() << "开始加载16位数据";
    emitProgress("计算16位数据范围...", 50);

    double minVal = 65535.0, maxVal = 0.0;
    std::vector<uint16_t> sampleBuffer(imageWidth_);
    GDALRasterBand* rasterBand = gdalDataset_->GetRasterBand(1);

    int sampleRows = std::min(100, imageHeight_);
    for (int i = 0; i < sampleRows; ++i) {
        int y = (i * imageHeight_) / sampleRows;
        if (rasterBand->RasterIO(GF_Read, 0, y, imageWidth_, 1,
            sampleBuffer.data(), imageWidth_, 1, GDT_UInt16, 0, 0) == CE_None) {
            for (uint16_t value : sampleBuffer) {
                minVal = std::min(minVal, static_cast<double>(value));
                maxVal = std::max(maxVal, static_cast<double>(value));
            }
        }
    }

    if (maxVal <= minVal) {
        maxVal = minVal + 1.0;
    }

    globalMin_ = minVal;
    globalMax_ = maxVal;
    hasGlobalRange_ = true;

    qDebug() << QString("16位数据范围: %1 - %2").arg(minVal).arg(maxVal);
    emitProgress("转换16位数据...", 60);

    if (bandCount_ == 1) {
        std::vector<uint16_t> buffer(static_cast<size_t>(imageWidth_) * imageHeight_);
        if (gdalDataset_->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, imageWidth_, imageHeight_,
            buffer.data(), imageWidth_, imageHeight_, GDT_UInt16, 0, 0) != CE_None) {
            return false;
        }

        for (int y = 0; y < imageHeight_; ++y) {
            unsigned char* line = image.scanLine(y);
            for (int x = 0; x < imageWidth_; ++x) {
                size_t idx = static_cast<size_t>(y) * imageWidth_ + x;
                int value = static_cast<int>(((buffer[idx] - minVal) * 255.0) / (maxVal - minVal));
                value = std::max(0, std::min(255, value));
                unsigned char byteValue = static_cast<unsigned char>(value);

                line[x * 3 + 0] = byteValue;
                line[x * 3 + 1] = byteValue;
                line[x * 3 + 2] = byteValue;
            }
            if (y % (imageHeight_ / 20) == 0) {
                int progress = 60 + (y * 30) / imageHeight_;
                emitProgress("转换16位数据...", progress);
                QApplication::processEvents();
            }
        }
        return true;
    }
    else if (bandCount_ >= 3) {
        std::vector<uint16_t> redBuffer(static_cast<size_t>(imageWidth_) * imageHeight_);
        std::vector<uint16_t> greenBuffer(static_cast<size_t>(imageWidth_) * imageHeight_);
        std::vector<uint16_t> blueBuffer(static_cast<size_t>(imageWidth_) * imageHeight_);

        if (gdalDataset_->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, imageWidth_, imageHeight_,
            redBuffer.data(), imageWidth_, imageHeight_, GDT_UInt16, 0, 0) != CE_None ||
            gdalDataset_->GetRasterBand(2)->RasterIO(GF_Read, 0, 0, imageWidth_, imageHeight_,
                greenBuffer.data(), imageWidth_, imageHeight_, GDT_UInt16, 0, 0) != CE_None ||
            gdalDataset_->GetRasterBand(3)->RasterIO(GF_Read, 0, 0, imageWidth_, imageHeight_,
                blueBuffer.data(), imageWidth_, imageHeight_, GDT_UInt16, 0, 0) != CE_None) {
            return false;
        }

        for (int y = 0; y < imageHeight_; ++y) {
            unsigned char* line = image.scanLine(y);
            for (int x = 0; x < imageWidth_; ++x) {
                size_t idx = static_cast<size_t>(y) * imageWidth_ + x;
                int redValue = static_cast<int>(((redBuffer[idx] - minVal) * 255.0) / (maxVal - minVal));
                int greenValue = static_cast<int>(((greenBuffer[idx] - minVal) * 255.0) / (maxVal - minVal));
                int blueValue = static_cast<int>(((blueBuffer[idx] - minVal) * 255.0) / (maxVal - minVal));

                line[x * 3 + 0] = std::max(0, std::min(255, redValue));
                line[x * 3 + 1] = std::max(0, std::min(255, greenValue));
                line[x * 3 + 2] = std::max(0, std::min(255, blueValue));
            }
            if (y % (imageHeight_ / 20) == 0) {
                int progress = 60 + (y * 30) / imageHeight_;
                emitProgress("转换16位多波段数据...", progress);
                QApplication::processEvents();
            }
        }
        return true;
    }
    return false;
}

bool ImageDisplayWidget::load32BitDataDirect(QImage& image)
{
    return load16BitDataDirect(image);
}

bool ImageDisplayWidget::loadFloatDataDirect(QImage& image)
{
    return load16BitDataDirect(image);
}

void ImageDisplayWidget::calculateGlobalDataRange()
{
    // ✅ 在函数开头添加：
    GDAL::ensureInitialized();
    if (!gdalDataset_ || bandCount_ <= 0) {
        hasGlobalRange_ = false;
        return;
    }

    // 对于8位数据，直接使用0-255
    if (dataType_ == GDT_Byte) {
        globalMin_ = 0.0;
        globalMax_ = 255.0;
        hasGlobalRange_ = true;
        qDebug() << "8位数据，使用固定范围: 0-255";
        return;
    }

    // 对于16位数据，使用高效的采样策略
    if (dataType_ == GDT_UInt16 || dataType_ == GDT_Int16) {
        GDALRasterBand* firstBand = gdalDataset_->GetRasterBand(1);
        double minVal, maxVal, meanVal, stdVal;

        if (firstBand->GetStatistics(FALSE, FALSE, &minVal, &maxVal, &meanVal, &stdVal) == CE_None) {
            globalMin_ = minVal;
            globalMax_ = maxVal;
            hasGlobalRange_ = true;
            qDebug() << QString("使用GDAL统计信息 - 范围: %1 - %2").arg(globalMin_).arg(globalMax_);
            return;
        }

        // 快速采样计算范围
        globalMin_ = 65535.0;
        globalMax_ = 0.0;

        int imageSize = imageWidth_ * imageHeight_;
        int sampleRate = imageSize > 100000000 ? 1000 : (imageSize > 25000000 ? 500 : 100);

        int stepX = std::max(1, imageWidth_ / sampleRate);
        int stepY = std::max(1, imageHeight_ / sampleRate);

        std::vector<uint16_t> sampleBuffer(stepX);
        GDALRasterBand* rasterBand = gdalDataset_->GetRasterBand(1);

        if (rasterBand) {
            for (int y = 0; y < imageHeight_; y += stepY) {
                if (rasterBand->RasterIO(GF_Read, 0, y, stepX, 1,
                    sampleBuffer.data(), stepX, 1, GDT_UInt16, 0, 0) == CE_None) {
                    for (uint16_t value : sampleBuffer) {
                        globalMin_ = std::min(globalMin_, (double)value);
                        globalMax_ = std::max(globalMax_, (double)value);
                    }
                }

                if (y % (stepY * 50) == 0) {
                    QApplication::processEvents();
                }
            }
        }

        if (globalMax_ <= globalMin_) {
            globalMax_ = globalMin_ + 1.0;
        }

        hasGlobalRange_ = true;
        qDebug() << QString("16位数据采样范围: %1 - %2").arg(globalMin_).arg(globalMax_);
        return;
    }

    // 其他数据类型的默认处理
    globalMin_ = 0.0;
    globalMax_ = 255.0;
    hasGlobalRange_ = true;
}

void ImageDisplayWidget::updateLoadTimerInterval()
{
    if (!loadTilesTimer_) return;

    int imageSize = imageWidth_ * imageHeight_;
    int interval;

    if (imageSize > 100000000) {
        interval = 300;
    }
    else if (imageSize > 25000000) {
        interval = 200;
    }
    else if (imageSize > 1000000) {
        interval = 150;
    }
    else {
        interval = 100;
    }

    loadTilesTimer_->setInterval(interval);
}

bool ImageDisplayWidget::loadTile(int x, int y)
{
    if (!gdalDataset_ || !hasGlobalRange_) {
        return false;
    }

    int tileX = x * tileSize_;
    int tileY = y * tileSize_;

    int w = std::min(tileSize_, imageWidth_ - tileX);
    int h = std::min(tileSize_, imageHeight_ - tileY);

    if (w <= 0 || h <= 0) {
        return false;
    }

    QImage image(w, h, bandCount_ == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    image.fill(Qt::black);

    bool loadSuccess = false;

    try {
        if (dataType_ == GDT_Byte) {
            if (bandCount_ == 1) {
                std::vector<unsigned char> buffer(w * h);
                if (gdalDataset_->GetRasterBand(1)->RasterIO(GF_Read, tileX, tileY, w, h,
                    buffer.data(), w, h, GDT_Byte, 0, 0) == CE_None) {
                    for (int j = 0; j < h; ++j) {
                        for (int i = 0; i < w; ++i) {
                            unsigned char value = buffer[j * w + i];
                            image.setPixel(i, j, qRgb(value, value, value));
                        }
                    }
                    loadSuccess = true;
                }
            }
            else if (bandCount_ >= 3) {
                std::vector<unsigned char> redBuffer(w * h);
                std::vector<unsigned char> greenBuffer(w * h);
                std::vector<unsigned char> blueBuffer(w * h);

                if (gdalDataset_->GetRasterBand(1)->RasterIO(GF_Read, tileX, tileY, w, h, redBuffer.data(), w, h, GDT_Byte, 0, 0) == CE_None &&
                    gdalDataset_->GetRasterBand(2)->RasterIO(GF_Read, tileX, tileY, w, h, greenBuffer.data(), w, h, GDT_Byte, 0, 0) == CE_None &&
                    gdalDataset_->GetRasterBand(3)->RasterIO(GF_Read, tileX, tileY, w, h, blueBuffer.data(), w, h, GDT_Byte, 0, 0) == CE_None) {
                    for (int j = 0; j < h; ++j) {
                        for (int i = 0; i < w; ++i) {
                            int idx = j * w + i;
                            image.setPixel(i, j, qRgb(redBuffer[idx], greenBuffer[idx], blueBuffer[idx]));
                        }
                    }
                    loadSuccess = true;
                }
            }
        }
        else if (dataType_ == GDT_UInt16 || dataType_ == GDT_Int16) {
            if (bandCount_ == 1) {
                std::vector<uint16_t> buffer(w * h);
                if (gdalDataset_->GetRasterBand(1)->RasterIO(GF_Read, tileX, tileY, w, h,
                    buffer.data(), w, h, GDT_UInt16, 0, 0) == CE_None) {
                    for (int j = 0; j < h; ++j) {
                        for (int i = 0; i < w; ++i) {
                            int idx = j * w + i;
                            int value = static_cast<int>(((buffer[idx] - globalMin_) * 255.0) / (globalMax_ - globalMin_));
                            value = std::max(0, std::min(255, value));
                            image.setPixel(i, j, qRgb(value, value, value));
                        }
                    }
                    loadSuccess = true;
                }
            }
            else if (bandCount_ >= 3) {
                std::vector<uint16_t> redBuffer(w * h);
                std::vector<uint16_t> greenBuffer(w * h);
                std::vector<uint16_t> blueBuffer(w * h);

                if (gdalDataset_->GetRasterBand(1)->RasterIO(GF_Read, tileX, tileY, w, h, redBuffer.data(), w, h, GDT_UInt16, 0, 0) == CE_None &&
                    gdalDataset_->GetRasterBand(2)->RasterIO(GF_Read, tileX, tileY, w, h, greenBuffer.data(), w, h, GDT_UInt16, 0, 0) == CE_None &&
                    gdalDataset_->GetRasterBand(3)->RasterIO(GF_Read, tileX, tileY, w, h, blueBuffer.data(), w, h, GDT_UInt16, 0, 0) == CE_None) {
                    for (int j = 0; j < h; ++j) {
                        for (int i = 0; i < w; ++i) {
                            int idx = j * w + i;
                            int r = static_cast<int>(((redBuffer[idx] - globalMin_) * 255.0) / (globalMax_ - globalMin_));
                            int g = static_cast<int>(((greenBuffer[idx] - globalMin_) * 255.0) / (globalMax_ - globalMin_));
                            int b = static_cast<int>(((blueBuffer[idx] - globalMin_) * 255.0) / (globalMax_ - globalMin_));

                            r = std::max(0, std::min(255, r));
                            g = std::max(0, std::min(255, g));
                            b = std::max(0, std::min(255, b));

                            image.setPixel(i, j, qRgb(r, g, b));
                        }
                    }
                    loadSuccess = true;
                }
            }
        }
    }
    catch (const std::exception& e) {
        qDebug() << QString("瓦片(%1,%2)加载异常: %3").arg(x).arg(y).arg(e.what());
        return false;
    }

    if (loadSuccess && imageScene_) {
        QGraphicsPixmapItem* item = imageScene_->addPixmap(QPixmap::fromImage(image));
        item->setPos(tileX, tileY);
        tileCache_[QPair<int, int>(x, y)] = item;
        return true;
    }

    return false;
}

void ImageDisplayWidget::loadVisibleTiles()
{
    if (!gdalDataset_ || currentLoadingMode_ != LoadingMode::Tiled) return;

    QRectF visibleRect = imageView_->mapToScene(imageView_->viewport()->rect()).boundingRect();

    if (visibleRect.isEmpty() || visibleRect.width() <= 0 || visibleRect.height() <= 0) {
        int centerX = imageWidth_ / 2;
        int centerY = imageHeight_ / 2;
        int size = std::min(imageWidth_, imageHeight_) / 4;
        visibleRect = QRectF(centerX - size / 2, centerY - size / 2, size, size);
    }

    int buffer = 1;
    int startX = std::max(0, static_cast<int>(visibleRect.left() / tileSize_) - buffer);
    int endX = std::min(tilesX_ - 1, static_cast<int>(visibleRect.right() / tileSize_) + buffer);
    int startY = std::max(0, static_cast<int>(visibleRect.top() / tileSize_) - buffer);
    int endY = std::min(tilesY_ - 1, static_cast<int>(visibleRect.bottom() / tileSize_) + buffer);

    QSet<QPair<int, int>> visibleTiles;
    for (int x = startX; x <= endX; ++x) {
        for (int y = startY; y <= endY; ++y) {
            visibleTiles.insert(QPair<int, int>(x, y));
        }
    }

    // 安全移除不再需要的瓦片
    QList<QPair<int, int>> tilesToRemove;
    for (auto it = tileCache_.begin(); it != tileCache_.end(); ++it) {
        if (!visibleTiles.contains(it.key())) {
            tilesToRemove.append(it.key());
        }
    }

    // 批量安全删除不需要的瓦片
    for (const auto& tile : tilesToRemove) {
        auto it = tileCache_.find(tile);
        if (it != tileCache_.end()) {
            QGraphicsPixmapItem* item = it.value();
            if (item && imageScene_) {
                try {
                    // 检查项目是否还在场景中
                    if (imageScene_->items().contains(item)) {
                        imageScene_->removeItem(item);
                    }
                    delete item;
                }
                catch (...) {
                    qDebug() << QString("删除瓦片(%1,%2)时发生异常").arg(tile.first).arg(tile.second);
                }
            }
            tileCache_.erase(it);
        }
    }

    // 加载新瓦片
    int maxTilesToLoad = 6;
    int imageSize = imageWidth_ * imageHeight_;
    if (imageSize > 100000000) {
        maxTilesToLoad = 3;
    }
    else if (imageSize > 25000000) {
        maxTilesToLoad = 4;
    }

    QList<QPair<int, int>> tilesToLoad;
    for (const auto& tilePos : visibleTiles) {
        if (!tileCache_.contains(tilePos)) {
            tilesToLoad.append(tilePos);
        }
    }

    // 按距离中心的距离排序
    int centerX = (startX + endX) / 2;
    int centerY = (startY + endY) / 2;

    std::sort(tilesToLoad.begin(), tilesToLoad.end(),
        [centerX, centerY](const QPair<int, int>& a, const QPair<int, int>& b) {
            int distA = (a.first - centerX) * (a.first - centerX) + (a.second - centerY) * (a.second - centerY);
            int distB = (b.first - centerX) * (b.first - centerX) + (b.second - centerY) * (b.second - centerY);
            return distA < distB;
        });

    int loadedTiles = 0;
    for (const auto& tilePos : tilesToLoad) {
        if (loadedTiles >= maxTilesToLoad) {
            if (tilesToLoad.size() > maxTilesToLoad) {
                QTimer::singleShot(100, this, [this]() {
                    loadVisibleTiles();
                    });
            }
            break;
        }

        if (loadTile(tilePos.first, tilePos.second)) {
            loadedTiles++;
        }

        if (loadedTiles % 2 == 0) {
            QApplication::processEvents();
        }
    }
}

void ImageDisplayWidget::loadVisibleTilesDelayed()
{
    if (currentLoadingMode_ == LoadingMode::Tiled) {
        needsReload_ = true;
        if (loadTilesTimer_) {
            loadTilesTimer_->start();
        }
    }
}

void ImageDisplayWidget::clearTileCache()
{
    qDebug() << "开始清理瓦片缓存，当前瓦片数量:" << tileCache_.size();

    // 安全地清理瓦片缓存
    if (!imageScene_) {
        qDebug() << "imageScene_为空，直接清空缓存";
        tileCache_.clear();
        return;
    }

    // 分步骤清理：先收集有效项目，再移除，最后删除
    QList<QGraphicsPixmapItem*> validItems;
    QList<QPair<int, int>> keysToRemove;

    // 第一步：收集有效的项目
    for (auto it = tileCache_.begin(); it != tileCache_.end(); ++it) {
        QGraphicsPixmapItem* item = it.value();
        if (item) {
            // 检查项目是否还在场景中
            QList<QGraphicsItem*> sceneItems = imageScene_->items();
            if (sceneItems.contains(item)) {
                validItems.append(item);
            }
            keysToRemove.append(it.key());
        }
    }

    qDebug() << "找到" << validItems.size() << "个有效瓦片项目需要清理";

    // 第二步：从场景中移除项目
    for (QGraphicsPixmapItem* item : validItems) {
        try {
            if (item && imageScene_) {
                imageScene_->removeItem(item);
            }
        }
        catch (...) {
            qDebug() << "移除瓦片项目时发生异常，跳过该项目";
            continue;
        }
    }

    // 第三步：清空缓存映射
    tileCache_.clear();

    // 第四步：安全删除项目
    for (QGraphicsPixmapItem* item : validItems) {
        try {
            if (item) {
                delete item;
                item = nullptr;
            }
        }
        catch (...) {
            qDebug() << "删除瓦片项目时发生异常，跳过该项目";
            continue;
        }
    }

    qDebug() << "瓦片缓存清理完成";
}

void ImageDisplayWidget::clearDisplay()
{
    qDebug() << "开始清理显示，当前加载模式:" << (currentLoadingMode_ == LoadingMode::Tiled ? "瓦片" : "直接");

    // 停止所有定时器
    if (loadTilesTimer_) {
        loadTilesTimer_->stop();
    }

    // 重置路径和尺寸信息
    currentImagePath_.clear();
    imageWidth_ = 0;
    imageHeight_ = 0;
    bandCount_ = 0;
    hasGlobalRange_ = false;
    needsReload_ = false;

    // 清理控制点显示（在清理场景之前）
    if (imageScene_ && controlPointManager_) {
        try {
            controlPointManager_->clearSceneDisplay(imageScene_, imageType_);
        }
        catch (...) {
            qDebug() << "清理控制点显示时发生异常";
        }
    }

    // 清理瓦片缓存（在清理场景之前）
    try {
        clearTileCache();
    }
    catch (...) {
        qDebug() << "清理瓦片缓存时发生异常";
        // 强制清空缓存
        tileCache_.clear();
    }

    // 最后清理场景
    if (imageScene_) {
        try {
            imageScene_->clear();
            imageScene_->setSceneRect(-50000, -50000, 100000, 100000); // 重置场景矩形
        }
        catch (...) {
            qDebug() << "清理场景时发生异常";
        }
    }

    // 清理GDAL资源
    cleanupGDALResources();

    // 重置加载模式
    currentLoadingMode_ = LoadingMode::Direct;

    // 更新按钮状态
    updateZoomButtons();

    // 发送缩放级别变化信号
    emit zoomLevelChanged(1.0);

    qDebug() << "显示清理完成";
}

void ImageDisplayWidget::setControlPointManager(ControlPointManager* manager)
{
    if (controlPointManager_) {
        disconnect(controlPointManager_, nullptr, this, nullptr);
    }

    controlPointManager_ = manager;

    if (imageView_) {
        imageView_->setControlPointManager(manager);
    }

    if (manager) {
        connect(manager,
            QOverload<int, ControlPointType>::of(&ControlPointManager::controlPointAdded),
            this,
            [this](int pointId, ControlPointType type) {
                Q_UNUSED(pointId)
                    if (type == imageType_) {
                        QTimer::singleShot(100, this, [this]() {
                            updateControlPointDisplay();
                            });
                    }
            },
            Qt::QueuedConnection);

        qDebug() << QString("为%1图像设置控制点管理器").arg(
            imageType_ == ControlPointType::Reference ? "参考" : "待配准");
    }
}

void ImageDisplayWidget::setControlPointMode(bool enabled)
{
    if (imageView_) {
        imageView_->setControlPointMode(enabled);
    }
}

void ImageDisplayWidget::updateControlPointDisplay()
{
    if (controlPointManager_ && imageScene_) {
        qDebug() << QString("更新%1图像的控制点显示").arg(
            imageType_ == ControlPointType::Reference ? "参考" : "待配准");

        controlPointManager_->updateSceneDisplay(imageScene_, imageType_);
        imageScene_->update();

        if (imageView_) {
            imageView_->update();
        }
    }
}

void ImageDisplayWidget::setToolBarVisible(bool visible)
{
    if (toolBarWidget_) {
        toolBarWidget_->setVisible(visible);
    }
}

QSize ImageDisplayWidget::getImageSize() const
{
    return QSize(imageWidth_, imageHeight_);
}

void ImageDisplayWidget::onViewScrolled()
{
    if (currentLoadingMode_ == LoadingMode::Tiled) {
        loadVisibleTilesDelayed();
    }
}

void ImageDisplayWidget::zoomIn()
{
    if (imageView_) {
        double currentZoom = getCurrentZoomLevel();
        double newZoom = std::min(MAX_ZOOM, currentZoom * (1.0 + ZOOM_STEP));
        setZoomLevel(newZoom);
    }
}

void ImageDisplayWidget::zoomOut()
{
    if (imageView_) {
        double currentZoom = getCurrentZoomLevel();
        double newZoom = std::max(MIN_ZOOM, currentZoom * (1.0 - ZOOM_STEP));
        setZoomLevel(newZoom);
    }
}

void ImageDisplayWidget::zoomToFit()
{
    if (imageView_ && hasImage()) {
        imageView_->fitInView(imageScene_->sceneRect(), Qt::KeepAspectRatio);
        emit zoomLevelChanged(getCurrentZoomLevel());
        updateZoomButtons();

        if (currentLoadingMode_ == LoadingMode::Tiled) {
            QTimer::singleShot(100, this, [this]() {
                loadVisibleTiles();
                });
        }
    }
}

void ImageDisplayWidget::zoomToActualSize()
{
    if (imageView_) {
        setZoomLevel(1.0);
    }
}

void ImageDisplayWidget::setZoomLevel(double level)
{
    if (imageView_) {
        level = std::max(MIN_ZOOM, std::min(MAX_ZOOM, level));
        QTransform transform;
        transform.scale(level, level);
        imageView_->setTransform(transform);
        emit zoomLevelChanged(level);
        updateZoomButtons();

        if (currentLoadingMode_ == LoadingMode::Tiled) {
            loadVisibleTilesDelayed();
        }
    }
}

double ImageDisplayWidget::getCurrentZoomLevel() const
{
    if (imageView_) {
        return imageView_->transform().m11();
    }
    return 1.0;
}

void ImageDisplayWidget::onZoomInClicked() { zoomIn(); }
void ImageDisplayWidget::onZoomOutClicked() { zoomOut(); }
void ImageDisplayWidget::onZoomFitClicked() { zoomToFit(); }
void ImageDisplayWidget::onZoomActualClicked() { zoomToActualSize(); }
void ImageDisplayWidget::onLoadTilesTimer() {
    if (needsReload_) {
        needsReload_ = false;
        loadVisibleTiles();
    }
}

void ImageDisplayWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0) {
            zoomIn();
        }
        else {
            zoomOut();
        }
        event->accept();
    }
    else {
        QWidget::wheelEvent(event);
    }
}

void ImageDisplayWidget::mousePressEvent(QMouseEvent* event) { QWidget::mousePressEvent(event); }
void ImageDisplayWidget::mouseMoveEvent(QMouseEvent* event) { QWidget::mouseMoveEvent(event); }

void ImageDisplayWidget::updateZoomButtons()
{
    if (!zoomInBtn_ || !zoomOutBtn_) return;
    double currentZoom = getCurrentZoomLevel();
    zoomInBtn_->setEnabled(currentZoom < MAX_ZOOM);
    zoomOutBtn_->setEnabled(currentZoom > MIN_ZOOM);
}

void ImageDisplayWidget::emitProgress(const QString& message, int value)
{
    emit progressUpdate(message, value);
    QApplication::processEvents();
}

void ImageDisplayWidget::cleanupGDALResources()
{
    if (gdalDataset_) {
        try {
            qDebug() << "关闭GDAL数据集";
            GDALClose(gdalDataset_);
            gdalDataset_ = nullptr;
            qDebug() << "GDAL数据集已关闭";
        }
        catch (...) {
            qDebug() << "关闭GDAL数据集时发生异常";
            gdalDataset_ = nullptr;
        }
    }
}

double ImageDisplayWidget::getCurrentTransformScale() const
{
    if (imageView_) {
        return imageView_->transform().m11();
    }
    return 1.0;
}