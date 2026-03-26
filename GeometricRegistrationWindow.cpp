#include "GeometricRegistrationWindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>
#include <QFileDialog>
#include <QGroupBox>
#include <QActionGroup>
#include <QStandardPaths>

GeometricRegistrationWindow::GeometricRegistrationWindow(ImageManager* imageManager, QWidget* parent)
    : QMainWindow(parent)
    , imageManager_(imageManager)
    , controlPointManager_(nullptr)
    , centralWidget_(nullptr)
    , mainLayout_(nullptr)
    , mainToolBar_(nullptr)
    , addPointAction_(nullptr)
    , deletePointAction_(nullptr)
    , zoomModeAction_(nullptr)
    , panModeAction_(nullptr)
    , resetViewAction_(nullptr)
    , titleLabel_(nullptr)
    , titleFrame_(nullptr)
    , imageSplitter_(nullptr)
    , refImageContainer_(nullptr)
    , targetImageContainer_(nullptr)
    , refImageLayout_(nullptr)
    , targetImageLayout_(nullptr)
    , refImageToolBar_(nullptr)
    , targetImageToolBar_(nullptr)
    , referenceImageWidget_(nullptr)
    , targetImageWidget_(nullptr)
    , controlPointDock_(nullptr)
    , controlPointTable_(nullptr)
    , controlPointWidget_(nullptr)
    , controlPointLayout_(nullptr)
    , selectReferenceBtn_(nullptr)
    , selectTargetBtn_(nullptr)
    , importControlPointsBtn_(nullptr)
    , exportControlPointsBtn_(nullptr)
    , clearAllControlPointsBtn_(nullptr)
    , bottomButtonWidget_(nullptr)
    , bottomButtonLayout_(nullptr)
    , saveBtn_(nullptr)
    , cancelBtn_(nullptr)
    , executeBtn_(nullptr)
    , pointCountLabel_(nullptr)
    , statusLabel_(nullptr)
    , referenceImageLoaded_(false)
    , targetImageLoaded_(false)
    , controlPointMode_(false)
{
    setWindowTitle("几何配准");
    setMinimumSize(1000, 700);
    resize(1400, 900);

    setupUI();

    // 创建控制点管理器
    controlPointManager_ = new ControlPointManager(this);

    // 设置图像组件的控制点管理器和图像类型 - 新增这部分
    if (referenceImageWidget_) {
        referenceImageWidget_->setControlPointManager(controlPointManager_);
        referenceImageWidget_->setImageType(ControlPointType::Reference);  // 设置为参考图像
    }
    if (targetImageWidget_) {
        targetImageWidget_->setControlPointManager(controlPointManager_);
        targetImageWidget_->setImageType(ControlPointType::Target);        // 设置为待配准图像
    }

    setupConnections();
    updateWindowTitle();
    updateStatus("就绪 - 请选择基准图像和待配准图像");
}

GeometricRegistrationWindow::~GeometricRegistrationWindow()
{
    if (controlPointManager_) {
        controlPointManager_->clearAllControlPoints();
    }

    if (referenceImageWidget_) {
        referenceImageWidget_->clearDisplay();
    }
    if (targetImageWidget_) {
        targetImageWidget_->clearDisplay();
    }
}

void GeometricRegistrationWindow::setupUI()
{
    // 创建中央控件
    centralWidget_ = new QWidget(this);
    setCentralWidget(centralWidget_);
    mainLayout_ = new QVBoxLayout(centralWidget_);
    mainLayout_->setContentsMargins(5, 5, 5, 5);
    mainLayout_->setSpacing(5);

    setupMainToolBar();
    setupImageDisplayArea();
    setupControlPointDock();
    setupBottomButtons();
    setupStatusBar();
}

void GeometricRegistrationWindow::setupMainToolBar()
{
    // 标题
    titleFrame_ = new QFrame();
    titleFrame_->setFrameStyle(QFrame::Box | QFrame::Raised);
    titleFrame_->setLineWidth(1);
    titleFrame_->setFixedHeight(40);

    QHBoxLayout* titleLayout = new QHBoxLayout(titleFrame_);
    titleLabel_ = new QLabel("几何配准 - [基准影像] vs [待配准影像]");
    titleLabel_->setStyleSheet("font-size: 14px; font-weight: bold; color: #2c3e50;");
    titleLabel_->setAlignment(Qt::AlignCenter);
    titleLayout->addWidget(titleLabel_);

    mainLayout_->addWidget(titleFrame_);

    // 工具栏
    mainToolBar_ = new QToolBar("主工具栏", this);
    mainToolBar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    mainToolBar_->setIconSize(QSize(24, 24));

    addPointAction_ = mainToolBar_->addAction("📍 添加控制点");
    addPointAction_->setCheckable(true);
    addPointAction_->setToolTip("点击图像添加控制点");

    deletePointAction_ = mainToolBar_->addAction("🗑️ 删除");
    deletePointAction_->setToolTip("删除选中的控制点");

    mainToolBar_->addSeparator();

    zoomModeAction_ = mainToolBar_->addAction("🔍 缩放");
    zoomModeAction_->setCheckable(true);
    zoomModeAction_->setChecked(true);
    zoomModeAction_->setToolTip("缩放模式");

    panModeAction_ = mainToolBar_->addAction("✋ 平移");
    panModeAction_->setCheckable(true);
    panModeAction_->setToolTip("平移模式");

    resetViewAction_ = mainToolBar_->addAction("🔄 重置");
    resetViewAction_->setToolTip("重置视图");

    // 创建工具栏组
    QActionGroup* modeGroup = new QActionGroup(this);
    modeGroup->addAction(addPointAction_);
    modeGroup->addAction(zoomModeAction_);
    modeGroup->addAction(panModeAction_);

    mainLayout_->addWidget(mainToolBar_);
}

void GeometricRegistrationWindow::setupImageDisplayArea()
{
    imageSplitter_ = new QSplitter(Qt::Horizontal, this);

    // 基准图像容器
    refImageContainer_ = new QWidget();
    refImageLayout_ = new QVBoxLayout(refImageContainer_);
    refImageLayout_->setContentsMargins(2, 2, 2, 2);
    refImageLayout_->setSpacing(2);

    QLabel* refTitle = new QLabel("基准影像");
    refTitle->setAlignment(Qt::AlignCenter);
    refTitle->setStyleSheet("QLabel { background-color: #3498db; color: white; padding: 6px; font-weight: bold; }");
    refImageLayout_->addWidget(refTitle);

    // 基准图像工具栏
    refImageToolBar_ = new QToolBar();
    refImageToolBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    refImageToolBar_->addAction("放大", this, &GeometricRegistrationWindow::onRefZoomIn);
    refImageToolBar_->addAction("缩小", this, &GeometricRegistrationWindow::onRefZoomOut);
    refImageToolBar_->addAction("自适应", this, &GeometricRegistrationWindow::onRefZoomFit);
    refImageToolBar_->addAction("实际大小", this, &GeometricRegistrationWindow::onRefZoomActual);
    refImageLayout_->addWidget(refImageToolBar_);

    // 基准图像显示组件
    referenceImageWidget_ = new ImageDisplayWidget(this);
    referenceImageWidget_->setToolBarVisible(false);
    refImageLayout_->addWidget(referenceImageWidget_, 1);

    // 待配准图像容器
    targetImageContainer_ = new QWidget();
    targetImageLayout_ = new QVBoxLayout(targetImageContainer_);
    targetImageLayout_->setContentsMargins(2, 2, 2, 2);
    targetImageLayout_->setSpacing(2);

    QLabel* targetTitle = new QLabel("待配准影像");
    targetTitle->setAlignment(Qt::AlignCenter);
    targetTitle->setStyleSheet("QLabel { background-color: #e74c3c; color: white; padding: 6px; font-weight: bold; }");
    targetImageLayout_->addWidget(targetTitle);

    // 待配准图像工具栏
    targetImageToolBar_ = new QToolBar();
    targetImageToolBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    targetImageToolBar_->addAction("放大", this, &GeometricRegistrationWindow::onTargetZoomIn);
    targetImageToolBar_->addAction("缩小", this, &GeometricRegistrationWindow::onTargetZoomOut);
    targetImageToolBar_->addAction("自适应", this, &GeometricRegistrationWindow::onTargetZoomFit);
    targetImageToolBar_->addAction("实际大小", this, &GeometricRegistrationWindow::onTargetZoomActual);
    targetImageLayout_->addWidget(targetImageToolBar_);

    // 待配准图像显示组件
    targetImageWidget_ = new ImageDisplayWidget(this);
    targetImageWidget_->setToolBarVisible(false);
    targetImageLayout_->addWidget(targetImageWidget_, 1);

    imageSplitter_->addWidget(refImageContainer_);
    imageSplitter_->addWidget(targetImageContainer_);
    imageSplitter_->setStretchFactor(0, 1);
    imageSplitter_->setStretchFactor(1, 1);

    mainLayout_->addWidget(imageSplitter_, 1);
}

void GeometricRegistrationWindow::setupControlPointDock()
{
    controlPointDock_ = new QDockWidget("控制点列表", this);
    controlPointDock_->setFixedWidth(320);
    controlPointDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    controlPointDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    controlPointWidget_ = new QWidget();
    controlPointLayout_ = new QVBoxLayout(controlPointWidget_);
    controlPointLayout_->setContentsMargins(5, 5, 5, 5);
    controlPointLayout_->setSpacing(5);

    // 图像选择按钮
    QGroupBox* imageGroup = new QGroupBox("图像选择");
    QVBoxLayout* imageLayout = new QVBoxLayout(imageGroup);

    selectReferenceBtn_ = new QPushButton("选择基准影像");
    selectTargetBtn_ = new QPushButton("选择待配准影像");

    imageLayout->addWidget(selectReferenceBtn_);
    imageLayout->addWidget(selectTargetBtn_);
    controlPointLayout_->addWidget(imageGroup);

    // 新增：控制点文件操作按钮组
    QGroupBox* fileGroup = new QGroupBox("控制点文件操作");
    QVBoxLayout* fileLayout = new QVBoxLayout(fileGroup);

    importControlPointsBtn_ = new QPushButton("📂 导入控制点");
    exportControlPointsBtn_ = new QPushButton("💾 导出控制点");
    clearAllControlPointsBtn_ = new QPushButton("🗑️ 清空所有控制点");

    // 设置按钮样式
    importControlPointsBtn_->setStyleSheet("QPushButton { text-align: left; padding: 6px; }");
    exportControlPointsBtn_->setStyleSheet("QPushButton { text-align: left; padding: 6px; }");
    clearAllControlPointsBtn_->setStyleSheet("QPushButton { text-align: left; padding: 6px; color: #c0392b; }");

    // 设置工具提示
    importControlPointsBtn_->setToolTip("从TXT文件导入控制点");
    exportControlPointsBtn_->setToolTip("导出控制点到TXT文件");
    clearAllControlPointsBtn_->setToolTip("删除所有控制点（不可撤销）");

    fileLayout->addWidget(importControlPointsBtn_);
    fileLayout->addWidget(exportControlPointsBtn_);
    fileLayout->addWidget(clearAllControlPointsBtn_);
    controlPointLayout_->addWidget(fileGroup);

    // 控制点表格
    QGroupBox* pointGroup = new QGroupBox("控制点列表");
    QVBoxLayout* pointLayout = new QVBoxLayout(pointGroup);

    controlPointTable_ = new QTableWidget(0, 3);
    QStringList headers;
    headers << "ID" << "X坐标" << "Y坐标";
    controlPointTable_->setHorizontalHeaderLabels(headers);

    controlPointTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    controlPointTable_->setAlternatingRowColors(true);
    controlPointTable_->horizontalHeader()->setStretchLastSection(true);
    controlPointTable_->setColumnWidth(0, 40);
    controlPointTable_->setColumnWidth(1, 80);
    controlPointTable_->setColumnWidth(2, 80);

    pointLayout->addWidget(controlPointTable_);
    controlPointLayout_->addWidget(pointGroup);

    controlPointDock_->setWidget(controlPointWidget_);
    addDockWidget(Qt::RightDockWidgetArea, controlPointDock_);
}

void GeometricRegistrationWindow::setupBottomButtons()
{
    bottomButtonWidget_ = new QWidget();
    bottomButtonLayout_ = new QHBoxLayout(bottomButtonWidget_);
    bottomButtonLayout_->setContentsMargins(10, 5, 10, 5);
    bottomButtonLayout_->setSpacing(10);

    bottomButtonLayout_->addStretch();

    saveBtn_ = new QPushButton("保存配准");
    cancelBtn_ = new QPushButton("取消");
    executeBtn_ = new QPushButton("执行配准");

    executeBtn_->setStyleSheet("QPushButton { background-color: #27ae60; color: white; font-weight: bold; padding: 8px 16px; }");
    cancelBtn_->setStyleSheet("QPushButton { background-color: #e74c3c; color: white; padding: 8px 16px; }");
    saveBtn_->setStyleSheet("QPushButton { background-color: #3498db; color: white; padding: 8px 16px; }");

    bottomButtonLayout_->addWidget(saveBtn_);
    bottomButtonLayout_->addWidget(cancelBtn_);
    bottomButtonLayout_->addWidget(executeBtn_);

    mainLayout_->addWidget(bottomButtonWidget_);
}

void GeometricRegistrationWindow::setupStatusBar()
{
    pointCountLabel_ = new QLabel("控制点数: 0");
    statusLabel_ = new QLabel("就绪");

    statusBar()->addWidget(statusLabel_);
    statusBar()->addPermanentWidget(pointCountLabel_);
}

void GeometricRegistrationWindow::setupConnections()
{
    // 主工具栏
    connect(addPointAction_, &QAction::triggered, this, &GeometricRegistrationWindow::onAddControlPoint);
    connect(deletePointAction_, &QAction::triggered, this, &GeometricRegistrationWindow::onDeleteControlPoint);
    connect(zoomModeAction_, &QAction::triggered, this, &GeometricRegistrationWindow::onZoomMode);
    connect(panModeAction_, &QAction::triggered, this, &GeometricRegistrationWindow::onPanMode);
    connect(resetViewAction_, &QAction::triggered, this, &GeometricRegistrationWindow::onResetView);

    // 图像选择
    connect(selectReferenceBtn_, &QPushButton::clicked, this, &GeometricRegistrationWindow::onSelectReferenceImage);
    connect(selectTargetBtn_, &QPushButton::clicked, this, &GeometricRegistrationWindow::onSelectTargetImage);

    // 新增：控制点文件操作按钮连接
    connect(importControlPointsBtn_, &QPushButton::clicked, this, &GeometricRegistrationWindow::onImportControlPoints);
    connect(exportControlPointsBtn_, &QPushButton::clicked, this, &GeometricRegistrationWindow::onExportControlPoints);
    connect(clearAllControlPointsBtn_, &QPushButton::clicked, this, &GeometricRegistrationWindow::onClearAllControlPoints);

    // 控制点表格
    connect(controlPointTable_, &QTableWidget::cellClicked, this, &GeometricRegistrationWindow::onControlPointTableClicked);

    // 底部按钮
    connect(saveBtn_, &QPushButton::clicked, this, &GeometricRegistrationWindow::onSaveRegistration);
    connect(cancelBtn_, &QPushButton::clicked, this, &GeometricRegistrationWindow::onCancelRegistration);
    connect(executeBtn_, &QPushButton::clicked, this, &GeometricRegistrationWindow::onExecuteRegistration);

    // 图像显示组件
    if (referenceImageWidget_) {
        connect(referenceImageWidget_, &ImageDisplayWidget::imageDisplayed,
            this, &GeometricRegistrationWindow::onReferenceImageDisplayed);
        connect(referenceImageWidget_, &ImageDisplayWidget::imageDisplayFailed,
            this, &GeometricRegistrationWindow::onReferenceImageFailed);
    }

    if (targetImageWidget_) {
        connect(targetImageWidget_, &ImageDisplayWidget::imageDisplayed,
            this, &GeometricRegistrationWindow::onTargetImageDisplayed);
        connect(targetImageWidget_, &ImageDisplayWidget::imageDisplayFailed,
            this, &GeometricRegistrationWindow::onTargetImageFailed);
    }

    // 控制点管理器
    if (controlPointManager_) {
        // 使用 QOverload 来处理重载的信号
        connect(controlPointManager_,
            QOverload<int, ControlPointType>::of(&ControlPointManager::controlPointAdded),
            this, &GeometricRegistrationWindow::onControlPointAdded);
        connect(controlPointManager_, &ControlPointManager::controlPointRemoved,
            this, &GeometricRegistrationWindow::onControlPointRemoved);
        connect(controlPointManager_, &ControlPointManager::controlPointsChanged,
            this, &GeometricRegistrationWindow::onControlPointsChanged);

        // 新增：控制点文件操作信号连接
        connect(controlPointManager_, &ControlPointManager::controlPointsImported,
            this, &GeometricRegistrationWindow::onControlPointsImported);
        connect(controlPointManager_, &ControlPointManager::controlPointsExported,
            this, &GeometricRegistrationWindow::onControlPointsExported);
        connect(controlPointManager_, &ControlPointManager::allControlPointsCleared,
            this, &GeometricRegistrationWindow::onAllControlPointsCleared);
    }
}

// 新增：导入控制点槽函数
void GeometricRegistrationWindow::onImportControlPoints()
{
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "导入控制点文件",
        defaultPath,
        "文本文件 (*.txt);;所有文件 (*)"
    );

    if (filePath.isEmpty()) {
        return;
    }

    if (!controlPointManager_) {
        QMessageBox::warning(this, "错误", "控制点管理器未初始化");
        return;
    }

    updateStatus("正在导入控制点...");

    if (controlPointManager_->importControlPoints(filePath)) {
        updateStatus("控制点导入成功");
        // 更新控制点显示
        QTimer::singleShot(100, this, [this]() {
            if (referenceImageWidget_ && referenceImageWidget_->hasImage()) {
                referenceImageWidget_->updateControlPointDisplay();
            }
            if (targetImageWidget_ && targetImageWidget_->hasImage()) {
                targetImageWidget_->updateControlPointDisplay();
            }
            });
    }
    else {
        QMessageBox::critical(this, "导入失败",
            QString("无法导入控制点文件：%1\n\n请检查文件格式是否正确。").arg(QFileInfo(filePath).fileName()));
        updateStatus("控制点导入失败");
    }
}

// 新增：导出控制点槽函数
void GeometricRegistrationWindow::onExportControlPoints()
{
    if (!controlPointManager_ || controlPointManager_->getControlPointCount() == 0) {
        QMessageBox::information(this, "导出控制点", "当前没有控制点可以导出。");
        return;
    }

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString defaultFileName = QString("控制点_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    QString filePath = QFileDialog::getSaveFileName(
        this,
        "导出控制点文件",
        QDir(defaultPath).filePath(defaultFileName),
        "文本文件 (*.txt);;所有文件 (*)"
    );

    if (filePath.isEmpty()) {
        return;
    }

    updateStatus("正在导出控制点...");

    if (controlPointManager_->exportControlPoints(filePath)) {
        updateStatus("控制点导出成功");
    }
    else {
        QMessageBox::critical(this, "导出失败",
            QString("无法导出控制点到文件：%1").arg(QFileInfo(filePath).fileName()));
        updateStatus("控制点导出失败");
    }
}

// 新增：清空所有控制点槽函数
void GeometricRegistrationWindow::onClearAllControlPoints()
{
    if (!controlPointManager_) {
        return;
    }

    // 使用控制点管理器的带确认功能
    controlPointManager_->clearAllControlPointsWithConfirmation();
}

// 新增：控制点导入成功后的处理
void GeometricRegistrationWindow::onControlPointsImported(int count)
{
    QMessageBox::information(this, "导入成功",
        QString("成功导入 %1 个控制点。").arg(count));
    updateControlPointInfo();
    checkEnableRegistration();
}

// 新增：控制点导出成功后的处理
void GeometricRegistrationWindow::onControlPointsExported(int count)
{
    QMessageBox::information(this, "导出成功",
        QString("成功导出 %1 个控制点。").arg(count));
}

// 新增：所有控制点清空后的处理
void GeometricRegistrationWindow::onAllControlPointsCleared()
{
    updateControlPointInfo();
    checkEnableRegistration();

    // 强制清除所有可能残留的控制点图形项
    if (controlPointManager_) {
        controlPointManager_->forceClearAllSceneItems();
    }

    // 强制更新图像上的控制点显示
    if (referenceImageWidget_) {
        referenceImageWidget_->updateControlPointDisplay();
        // 强制刷新场景
        QTimer::singleShot(50, [this]() {
            if (referenceImageWidget_) {
                referenceImageWidget_->update();
            }
            });
    }

    if (targetImageWidget_) {
        targetImageWidget_->updateControlPointDisplay();
        // 强制刷新场景
        QTimer::singleShot(50, [this]() {
            if (targetImageWidget_) {
                targetImageWidget_->update();
            }
            });
    }

    updateStatus("所有控制点已清空");
}

// 工具栏操作槽函数
void GeometricRegistrationWindow::onAddControlPoint()
{
    controlPointMode_ = addPointAction_->isChecked();

    if (referenceImageWidget_) {
        referenceImageWidget_->setControlPointMode(controlPointMode_);
    }
    if (targetImageWidget_) {
        targetImageWidget_->setControlPointMode(controlPointMode_);
    }

    if (controlPointMode_) {
        updateStatus("控制点添加模式已开启 - 点击图像添加控制点");
    }
    else {
        updateStatus("控制点添加模式已关闭");
    }
}

// 修改删除控制点的方法
void GeometricRegistrationWindow::onDeleteControlPoint()
{
    int currentRow = controlPointTable_->currentRow();
    if (currentRow >= 0 && currentRow < controlPointTable_->rowCount()) {
        QTableWidgetItem* idItem = controlPointTable_->item(currentRow, 0);
        if (idItem) {
            int pointId = idItem->text().toInt();
            if (controlPointManager_) {
                // 确认删除
                QMessageBox::StandardButton reply = QMessageBox::question(this,
                    "删除控制点",
                    QString("确定要删除控制点 %1 吗？").arg(pointId),
                    QMessageBox::Yes | QMessageBox::No);

                if (reply == QMessageBox::Yes) {
                    if (controlPointManager_->removeControlPoint(pointId)) {
                        updateStatus(QString("已删除控制点 %1").arg(pointId));

                        // 手动更新表格和状态，但不重绘场景
                        controlPointManager_->updateTableWidget(controlPointTable_);
                        updateControlPointInfo();
                        checkEnableRegistration();
                    }
                }
            }
        }
    }
    else {
        updateStatus("请先选择要删除的控制点");
    }
}

void GeometricRegistrationWindow::onZoomMode()
{
    controlPointMode_ = false;
    if (referenceImageWidget_) {
        referenceImageWidget_->setControlPointMode(false);
    }
    if (targetImageWidget_) {
        targetImageWidget_->setControlPointMode(false);
    }
    updateStatus("缩放模式");
}

void GeometricRegistrationWindow::onPanMode()
{
    controlPointMode_ = false;
    if (referenceImageWidget_) {
        referenceImageWidget_->setControlPointMode(false);
    }
    if (targetImageWidget_) {
        targetImageWidget_->setControlPointMode(false);
    }
    updateStatus("平移模式");
}

void GeometricRegistrationWindow::onResetView()
{
    if (referenceImageWidget_) referenceImageWidget_->zoomToFit();
    if (targetImageWidget_) targetImageWidget_->zoomToFit();
    updateStatus("视图已重置");
}

// 图像工具栏槽函数
void GeometricRegistrationWindow::onRefZoomIn()
{
    if (referenceImageWidget_) referenceImageWidget_->zoomIn();
}

void GeometricRegistrationWindow::onRefZoomOut()
{
    if (referenceImageWidget_) referenceImageWidget_->zoomOut();
}

void GeometricRegistrationWindow::onRefZoomFit()
{
    if (referenceImageWidget_) referenceImageWidget_->zoomToFit();
}

void GeometricRegistrationWindow::onRefZoomActual()
{
    if (referenceImageWidget_) referenceImageWidget_->zoomToActualSize();
}

void GeometricRegistrationWindow::onTargetZoomIn()
{
    if (targetImageWidget_) targetImageWidget_->zoomIn();
}

void GeometricRegistrationWindow::onTargetZoomOut()
{
    if (targetImageWidget_) targetImageWidget_->zoomOut();
}

void GeometricRegistrationWindow::onTargetZoomFit()
{
    if (targetImageWidget_) targetImageWidget_->zoomToFit();
}

void GeometricRegistrationWindow::onTargetZoomActual()
{
    if (targetImageWidget_) targetImageWidget_->zoomToActualSize();
}

// 图像选择和显示
void GeometricRegistrationWindow::onSelectReferenceImage()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "选择基准影像",
        QString(),
        "图像文件 (*.tif *.tiff *.img *.png *.jpg *.jpeg *.bmp);;所有文件 (*)"
    );

    if (!filePath.isEmpty()) {
        setReferenceImage(filePath);
    }
}

void GeometricRegistrationWindow::onSelectTargetImage()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "选择待配准影像",
        QString(),
        "图像文件 (*.tif *.tiff *.img *.png *.jpg *.jpeg *.bmp);;所有文件 (*)"
    );

    if (!filePath.isEmpty()) {
        setTargetImage(filePath);
    }
}

void GeometricRegistrationWindow::setReferenceImage(const QString& imagePath)
{
    if (imagePath.isEmpty()) return;

    referenceImagePath_ = imagePath;
    referenceImageLoaded_ = false;

    if (referenceImageWidget_) {
        updateStatus("正在加载基准图像: " + QFileInfo(imagePath).fileName());
        referenceImageWidget_->displayImage(imagePath);
    }

    updateWindowTitle();
}

void GeometricRegistrationWindow::setTargetImage(const QString& imagePath)
{
    if (imagePath.isEmpty()) return;

    targetImagePath_ = imagePath;
    targetImageLoaded_ = false;

    if (targetImageWidget_) {
        updateStatus("正在加载待配准图像: " + QFileInfo(imagePath).fileName());
        targetImageWidget_->displayImage(imagePath);
    }

    updateWindowTitle();
}

void GeometricRegistrationWindow::onReferenceImageDisplayed(const QString& filePath, int width, int height, int bandCount)
{
    Q_UNUSED(filePath);
    referenceImageLoaded_ = true;
    updateStatus(QString("基准图像已加载: %1×%2像素, %3个波段").arg(width).arg(height).arg(bandCount));
    checkEnableRegistration();
}

void GeometricRegistrationWindow::onTargetImageDisplayed(const QString& filePath, int width, int height, int bandCount)
{
    Q_UNUSED(filePath);
    targetImageLoaded_ = true;
    updateStatus(QString("待配准图像已加载: %1×%2像素, %3个波段").arg(width).arg(height).arg(bandCount));
    checkEnableRegistration();
}

void GeometricRegistrationWindow::onReferenceImageFailed(const QString& errorMessage)
{
    referenceImageLoaded_ = false;
    updateStatus("基准图像加载失败: " + errorMessage);
    QMessageBox::critical(this, "图像加载失败", "基准图像加载失败:\n" + errorMessage);
}

void GeometricRegistrationWindow::onTargetImageFailed(const QString& errorMessage)
{
    targetImageLoaded_ = false;
    updateStatus("待配准图像加载失败: " + errorMessage);
    QMessageBox::critical(this, "图像加载失败", "待配准图像加载失败:\n" + errorMessage);
}

void GeometricRegistrationWindow::onControlPointAdded(int pointId, ControlPointType type)
{
    QString typeStr = (type == ControlPointType::Reference) ? "参考图像" : "待配准图像";
    updateStatus(QString("在%1上添加控制点 %2").arg(typeStr).arg(pointId));

    // 发出 controlPointsChanged 信号来更新表格
    QTimer::singleShot(50, this, [this]() {
        if (controlPointManager_) {
            emit controlPointManager_->controlPointsChanged();
        }
        });
}

void GeometricRegistrationWindow::onControlPointRemoved(int pointId)
{
    Q_UNUSED(pointId)
        // 什么都不做，因为图形已经在 removeControlPoint 中被删除了
        // 表格会在 onDeleteControlPoint 中手动更新
}

// 关键修改：简化 controlPointsChanged 处理
void GeometricRegistrationWindow::onControlPointsChanged()
{
    if (controlPointManager_) {
        // 只更新表格，不重绘图像
        controlPointManager_->updateTableWidget(controlPointTable_);
    }
    updateControlPointInfo();
    checkEnableRegistration();
}

void GeometricRegistrationWindow::onControlPointTableClicked(int row, int column)
{
    Q_UNUSED(column);
    if (row >= 0) {
        QTableWidgetItem* idItem = controlPointTable_->item(row, 0);
        if (idItem) {
            int pointId = idItem->text().toInt();
            updateStatus(QString("选中控制点 %1").arg(pointId));
        }
    }
}

// 修改 onExecuteRegistration 方法：
void GeometricRegistrationWindow::onExecuteRegistration()
{
    if (!controlPointManager_) return;

    int completePointCount = controlPointManager_->getCompleteControlPointCount();
    if (completePointCount < 3) {
        QMessageBox::warning(this, "执行配准",
            QString("至少需要3个完整的控制点才能执行配准\n当前完整控制点数量: %1").arg(completePointCount));
        return;
    }

    updateStatus("正在执行几何配准...");

    // 模拟配准过程
    QTimer::singleShot(2000, this, [this]() {
        updateStatus("几何配准执行完成");
        QMessageBox::information(this, "配准完成", "几何配准执行完成！");
        });
}

void GeometricRegistrationWindow::onSaveRegistration()
{
    QString outputPath = QFileDialog::getSaveFileName(
        this,
        "保存配准结果",
        QString(),
        "TIFF文件 (*.tif *.tiff);;所有文件 (*)"
    );

    if (!outputPath.isEmpty()) {
        updateStatus("正在保存配准结果...");
        emit registrationCompleted(outputPath);
        updateStatus("配准结果已保存");
    }
}

void GeometricRegistrationWindow::onCancelRegistration()
{
    close();
}

// 状态更新方法
void GeometricRegistrationWindow::updateWindowTitle()
{
    QString refName = referenceImagePath_.isEmpty() ? "基准影像" : QFileInfo(referenceImagePath_).fileName();
    QString tarName = targetImagePath_.isEmpty() ? "待配准影像" : QFileInfo(targetImagePath_).fileName();

    titleLabel_->setText(QString("几何配准 - [%1] vs [%2]").arg(refName).arg(tarName));
}

// 修改 updateControlPointInfo 方法：
void GeometricRegistrationWindow::updateControlPointInfo()
{
    if (!controlPointManager_) return;

    int totalCount = controlPointManager_->getControlPointCount();
    int completeCount = controlPointManager_->getCompleteControlPointCount();
    pointCountLabel_->setText(QString("控制点数: %1 (完整: %2)").arg(totalCount).arg(completeCount));
}

void GeometricRegistrationWindow::updateStatus(const QString& message)
{
    if (statusLabel_) {
        statusLabel_->setText(message);
    }
    qDebug() << "Status:" << message;
}

void GeometricRegistrationWindow::checkEnableRegistration()
{
    bool canRegister = referenceImageLoaded_ && targetImageLoaded_;

    if (controlPointManager_) {
        // 至少需要3个完整的控制点才能配准
        canRegister = canRegister && controlPointManager_->getCompleteControlPointCount() >= 3;
    }

    if (executeBtn_) executeBtn_->setEnabled(canRegister);
    if (saveBtn_) saveBtn_->setEnabled(canRegister);
}

void GeometricRegistrationWindow::closeEvent(QCloseEvent* event)
{
    emit windowClosed();
    QMainWindow::closeEvent(event);
}