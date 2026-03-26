#include "MainWindow.h"
#include "ui_mainwindow.h"
#include <QApplication>
#include <QImageReader>
#include <QFileInfo>
#include <QDebug>
#include <QKeySequence>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLayout>
#include <QSplitter>
#include <QProgressBar>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QListWidget>
#include <algorithm>
#include <cmath>
#include <QResizeEvent>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QAbstractButton>
#include <QProgressDialog>  // 确保包含这个
#include <QTime>        // 添加这一行用于计时
#include <QDebug>       // 确保包含调试功能
#include <QFileInfo>    // 确保包含文件信息功能
#include <QRegularExpression>
#include "HIS.h"
#include "PCA.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , imageManager(new ImageManager(this))
    , projectManager(new ProjectManager(this))
    , imageDisplayWidget(nullptr)
    , progressBar(nullptr)
    , statusLabel(nullptr)
    , recentProjectsMenu(nullptr)
    , registrationWindow(nullptr)  // 添加这一行
{
    ui->setupUi(this);
    // 设置窗口大小
    resize(1200, 800);
    // 设置TIF图像支持
    setupImageFormats();
    // 创建图像显示组件
    imageDisplayWidget = new ImageDisplayWidget(this);
    setupConnections();
    connectProjectManager();
    connectImageDisplayWidget();
    setupRecentProjectsMenu();

    // 连接几何配准菜单的信号槽
    setupGeometricRegistrationConnections();
    // 添加这一行
    setupImageFusionConnections();
    setupLayout();
    setupStatusBar();
    setupImageListControls();  // 添加这一行
    // 初始化界面状态
    updateImageList();
    // 设置窗口标题
    setWindowTitle("遥感图像处理系统");
}

MainWindow::~MainWindow()
{
    // 在析构前先安全关闭配准窗口
    if (registrationWindow) {
        // 断开所有信号连接
        disconnect(registrationWindow, nullptr, this, nullptr);
        registrationWindow->close();
        delete registrationWindow;  // 直接删除，不使用deleteLater
        registrationWindow = nullptr;
    }

    // 清理图像显示组件
    if (imageDisplayWidget) {
        imageDisplayWidget->clearDisplay();
    }

    delete ui;
}

void MainWindow::setupImageListControls()
{
    // 检查 dockWidget 是否存在
    QDockWidget* dockWidget = findChild<QDockWidget*>("dockWidget");
    if (!dockWidget) {
        qDebug() << "警告：未找到dockWidget";
        return;
    }

    // 获取 dockWidget 的内容widget
    QWidget* dockContent = dockWidget->widget();
    if (!dockContent) {
        qDebug() << "警告：dockWidget没有内容widget";
        return;
    }

    // 创建新的布局来包含控制按钮和图像列表
    QVBoxLayout* dockLayout = new QVBoxLayout(dockContent);
    dockLayout->setContentsMargins(5, 5, 5, 5);
    dockLayout->setSpacing(5);

    // 创建控制按钮容器
    QWidget* controlWidget = new QWidget();
    QVBoxLayout* controlLayout = new QVBoxLayout(controlWidget);
    controlLayout->setContentsMargins(2, 2, 2, 2);
    controlLayout->setSpacing(3);

    // 创建按钮
    selectAllBtn = new QPushButton("全选");
    unselectAllBtn = new QPushButton("取消全选");
    invertSelectionBtn = new QPushButton("反选");
    selectionCountLabel = new QLabel("已选择: 0");

    // 设置按钮大小
    selectAllBtn->setFixedHeight(25);
    unselectAllBtn->setFixedHeight(25);
    invertSelectionBtn->setFixedHeight(25);

    // 设置按钮样式
    QString buttonStyle =
        "QPushButton { "
        "    padding: 3px 8px; "
        "    border: 1px solid #ccc; "
        "    border-radius: 3px; "
        "    background-color: #f5f5f5; "
        "    font-size: 11px; "
        "} "
        "QPushButton:hover { "
        "    background-color: #e5e5e5; "
        "} "
        "QPushButton:pressed { "
        "    background-color: #d5d5d5; "
        "}";

    selectAllBtn->setStyleSheet(buttonStyle);
    unselectAllBtn->setStyleSheet(buttonStyle);
    invertSelectionBtn->setStyleSheet(buttonStyle);

    // 设置选择计数标签样式
    selectionCountLabel->setStyleSheet(
        "QLabel { "
        "    color: #666; "
        "    font-size: 11px; "
        "    padding: 2px; "
        "}"
    );
    selectionCountLabel->setAlignment(Qt::AlignCenter);

    // 创建按钮布局
    QHBoxLayout* buttonLayout1 = new QHBoxLayout();
    buttonLayout1->addWidget(selectAllBtn);
    buttonLayout1->addWidget(unselectAllBtn);

    QHBoxLayout* buttonLayout2 = new QHBoxLayout();
    buttonLayout2->addWidget(invertSelectionBtn);

    // 添加到控制布局
    controlLayout->addLayout(buttonLayout1);
    controlLayout->addLayout(buttonLayout2);
    controlLayout->addWidget(selectionCountLabel);

    // 从原来的位置移除 imageListWidget
    if (ui->imageListWidget->parent()) {
        ui->imageListWidget->setParent(nullptr);
    }

    // 添加到新布局
    dockLayout->addWidget(controlWidget);
    dockLayout->addWidget(ui->imageListWidget, 1); // 给图像列表更多空间

    // 连接信号
    connect(selectAllBtn, &QPushButton::clicked, this, &MainWindow::onSelectAllImages);
    connect(unselectAllBtn, &QPushButton::clicked, this, &MainWindow::onUnselectAllImages);
    connect(invertSelectionBtn, &QPushButton::clicked, this, &MainWindow::onInvertSelection);

    // 连接图像列表的项目变化信号
    connect(ui->imageListWidget, &QListWidget::itemChanged, this, &MainWindow::onImageListItemChanged);

    // 初始化按钮状态
    selectAllBtn->setEnabled(false);
    unselectAllBtn->setEnabled(false);
    invertSelectionBtn->setEnabled(false);

    qDebug() << "图像列表控制按钮设置完成";
}

void MainWindow::updateImageList()
{
    if (!ui->imageListWidget) {
        qDebug() << "警告：imageListWidget不存在";
        return;
    }

    // 断开信号以避免在更新过程中触发
    disconnect(ui->imageListWidget, &QListWidget::itemChanged, this, &MainWindow::onImageListItemChanged);

    ui->imageListWidget->clear();

    QList<ImageInfo> allImages = imageManager->getAllImages();
    for (const ImageInfo& info : allImages) {
        QListWidgetItem* item = new QListWidgetItem(info.fileName);

        // *** 关键：设置项目可选中 ***
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked); // 默认未选中

        item->setData(Qt::UserRole, info.filePath);
        item->setToolTip(QString("%1\n类型: %2\n尺寸: %3x%4\n波段数: %5")
            .arg(info.filePath)
            .arg(info.type == ImageType::Panchromatic ? "全色" :
                info.type == ImageType::Multispectral ? "多光谱" : "未知")
            .arg(info.width)
            .arg(info.height)
            .arg(info.bandCount));

        if (!info.thumbnail.isNull()) {
            item->setIcon(QIcon(info.thumbnail));
        }

        ui->imageListWidget->addItem(item);
    }

    // 重新连接信号
    connect(ui->imageListWidget, &QListWidget::itemChanged, this, &MainWindow::onImageListItemChanged);

    // 更新按钮状态和选择计数
    updateSelectionCount();

    // 更新按钮启用状态
    bool hasItems = allImages.size() > 0;
    if (selectAllBtn) selectAllBtn->setEnabled(hasItems);
    if (unselectAllBtn) unselectAllBtn->setEnabled(hasItems);
    if (invertSelectionBtn) invertSelectionBtn->setEnabled(hasItems);
}

void MainWindow::onImageListItemChanged(QListWidgetItem* item)
{
    Q_UNUSED(item)
        updateSelectionCount();

    // 可以在这里添加其他逻辑，比如自动处理选中的图像
    qDebug() << "图像选择状态改变，当前选中:" << getSelectedImageCount() << "个";
}

void MainWindow::onSelectAllImages()
{
    if (!ui->imageListWidget) return;

    // 临时断开信号以提高性能
    disconnect(ui->imageListWidget, &QListWidget::itemChanged, this, &MainWindow::onImageListItemChanged);

    for (int i = 0; i < ui->imageListWidget->count(); ++i) {
        QListWidgetItem* item = ui->imageListWidget->item(i);
        if (item) {
            item->setCheckState(Qt::Checked);
        }
    }

    // 重新连接信号
    connect(ui->imageListWidget, &QListWidget::itemChanged, this, &MainWindow::onImageListItemChanged);

    updateSelectionCount();
    qDebug() << "已全选所有图像";
}

void MainWindow::onUnselectAllImages()
{
    if (!ui->imageListWidget) return;

    // 临时断开信号以提高性能
    disconnect(ui->imageListWidget, &QListWidget::itemChanged, this, &MainWindow::onImageListItemChanged);

    for (int i = 0; i < ui->imageListWidget->count(); ++i) {
        QListWidgetItem* item = ui->imageListWidget->item(i);
        if (item) {
            item->setCheckState(Qt::Unchecked);
        }
    }

    // 重新连接信号
    connect(ui->imageListWidget, &QListWidget::itemChanged, this, &MainWindow::onImageListItemChanged);

    updateSelectionCount();
    qDebug() << "已取消选择所有图像";
}

void MainWindow::onInvertSelection()
{
    if (!ui->imageListWidget) return;

    // 临时断开信号以提高性能
    disconnect(ui->imageListWidget, &QListWidget::itemChanged, this, &MainWindow::onImageListItemChanged);

    for (int i = 0; i < ui->imageListWidget->count(); ++i) {
        QListWidgetItem* item = ui->imageListWidget->item(i);
        if (item) {
            Qt::CheckState currentState = item->checkState();
            item->setCheckState(currentState == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    }

    // 重新连接信号
    connect(ui->imageListWidget, &QListWidget::itemChanged, this, &MainWindow::onImageListItemChanged);

    updateSelectionCount();
    qDebug() << "已反选图像";
}

void MainWindow::updateSelectionCount()
{
    int selectedCount = getSelectedImageCount();
    int totalCount = ui->imageListWidget ? ui->imageListWidget->count() : 0;

    if (selectionCountLabel) {
        selectionCountLabel->setText(QString("已选择: %1/%2").arg(selectedCount).arg(totalCount));
    }

    // 更新状态栏信息
    if (selectedCount > 0) {
        ui->statusbar->showMessage(QString("已选择 %1 个图像").arg(selectedCount), 2000);
    }
}

QStringList MainWindow::getSelectedImagePaths() const
{
    QStringList selectedPaths;

    if (!ui->imageListWidget) return selectedPaths;

    for (int i = 0; i < ui->imageListWidget->count(); ++i) {
        QListWidgetItem* item = ui->imageListWidget->item(i);
        if (item && item->checkState() == Qt::Checked) {
            QString filePath = item->data(Qt::UserRole).toString();
            if (!filePath.isEmpty()) {
                selectedPaths.append(filePath);
            }
        }
    }

    return selectedPaths;
}

QList<ImageInfo> MainWindow::getSelectedImages() const
{
    QList<ImageInfo> selectedImages;
    QStringList selectedPaths = getSelectedImagePaths();

    for (const QString& path : selectedPaths) {
        ImageInfo info = imageManager->getImageInfo(path);
        if (!info.filePath.isEmpty()) {
            selectedImages.append(info);
        }
    }

    return selectedImages;
}

int MainWindow::getSelectedImageCount() const
{
    int count = 0;

    if (!ui->imageListWidget) return count;

    for (int i = 0; i < ui->imageListWidget->count(); ++i) {
        QListWidgetItem* item = ui->imageListWidget->item(i);
        if (item && item->checkState() == Qt::Checked) {
            count++;
        }
    }

    return count;
}


void MainWindow::connectProjectManager()
{
    connect(projectManager, &ProjectManager::projectOpened, this, &MainWindow::onProjectOpened);
    connect(projectManager, &ProjectManager::projectClosed, this, &MainWindow::onProjectClosed);
    qDebug() << "工程管理器信号已连接";
}

void MainWindow::connectImageDisplayWidget()
{
    connect(imageDisplayWidget, &ImageDisplayWidget::imageDisplayed,
        this, &MainWindow::onImageDisplayed);
    connect(imageDisplayWidget, &ImageDisplayWidget::imageDisplayFailed,
        this, &MainWindow::onImageDisplayFailed);
    connect(imageDisplayWidget, &ImageDisplayWidget::zoomLevelChanged,
        this, &MainWindow::onZoomLevelChanged);
    connect(imageDisplayWidget, &ImageDisplayWidget::progressUpdate,
        this, &MainWindow::onImageProgressUpdate);
    connect(imageDisplayWidget, &ImageDisplayWidget::progressFinished,
        this, &MainWindow::onImageProgressFinished);
    connect(imageDisplayWidget, &ImageDisplayWidget::tileLoadProgress,
        this, &MainWindow::onTileLoadProgress);

    qDebug() << "图像显示组件信号已连接";
}

void MainWindow::setupRecentProjectsMenu()
{
    // 安全获取最近工程菜单
    recentProjectsMenu = ui->menubar->findChild<QMenu*>("menuRecentProjects");
    if (!recentProjectsMenu) {
        // 如果没有找到，尝试其他可能的名称
        recentProjectsMenu = ui->menubar->findChild<QMenu*>("recentProjectsMenu");
    }

    if (recentProjectsMenu) {
        updateRecentProjectsMenu();
        qDebug() << "最近工程菜单已设置完成";
    }
    else {
        qDebug() << "警告：未找到最近工程菜单";
    }
}

void MainWindow::updateRecentProjectsMenu()
{
    if (!recentProjectsMenu) return;

    recentProjectsMenu->clear();
    QStringList recentProjects = projectManager->getRecentProjects();

    if (recentProjects.isEmpty()) {
        QAction* noRecentAction = recentProjectsMenu->addAction("无最近工程");
        noRecentAction->setEnabled(false);
        return;
    }

    for (int i = 0; i < recentProjects.size() && i < 10; ++i) {
        const QString& projectPath = recentProjects[i];
        QFileInfo fileInfo(projectPath);

        if (!fileInfo.exists()) continue;

        QString actionText = QString("%1. %2").arg(i + 1).arg(fileInfo.baseName());
        QAction* recentAction = recentProjectsMenu->addAction(actionText);
        recentAction->setData(projectPath);
        recentAction->setToolTip(projectPath);

        connect(recentAction, &QAction::triggered, this, &MainWindow::onOpenRecentProject);
    }

    if (!recentProjects.isEmpty()) {
        recentProjectsMenu->addSeparator();
        QAction* clearAction = recentProjectsMenu->addAction("清除历史记录");
        connect(clearAction, &QAction::triggered, this, &MainWindow::onClearRecentProjects);
    }
}

void MainWindow::onOpenRecentProject()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    QString projectPath = action->data().toString();
    if (projectPath.isEmpty() || !QFileInfo::exists(projectPath)) {
        QMessageBox::warning(this, "错误", "工程文件不存在！");
        updateRecentProjectsMenu();
        return;
    }

    if (projectManager->hasCurrentProject()) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "打开工程", "当前工程可能有未保存的更改。是否要先保存？",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) onSaveProject();
    }

    // 使用状态栏进度条显示进度
    showProgress("正在打开最近工程...", 30);
    imageManager->clearAllImages();
    imageDisplayWidget->clearDisplay();

    // 延迟执行，让界面有时间更新
    QTimer::singleShot(100, this, [this, projectPath]() {
        updateProgress("正在加载工程数据...", 60);

        QTimer::singleShot(200, this, [this, projectPath]() {
            if (projectManager->openProject(projectPath)) {
                updateProgress("正在恢复图像列表...", 80);
                loadProjectData();
                updateImageList();
                updateRecentProjectsMenu();

                hideDialogProgress("工程打开成功", 1500);
                ui->statusbar->showMessage("最近工程打开成功: " + QFileInfo(projectPath).fileName(), 3000);
                setWindowTitle("遥感图像处理系统 - " + QFileInfo(projectPath).baseName());
            }
            else {
                hideDialogProgress("打开失败", 1000);
                QMessageBox::warning(this, "错误", "无法打开工程文件: " + projectPath);
            }
            });
        });
}

void MainWindow::onClearRecentProjects()
{
    if (QMessageBox::question(this, "清除历史记录", "确定要清除所有最近工程记录吗？",
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        projectManager->clearRecentProjects();
        updateRecentProjectsMenu();
        ui->statusbar->showMessage("最近工程记录已清除", 2000);
    }
}

void MainWindow::onProjectOpened(const QString& projectPath)
{
    Q_UNUSED(projectPath)
        updateRecentProjectsMenu();
}

void MainWindow::onProjectClosed()
{
    setWindowTitle("遥感图像处理系统");
}

void MainWindow::setupImageFormats()
{
    QList<QByteArray> supportedFormats = QImageReader::supportedImageFormats();
    qDebug() << "支持的图像格式:" << supportedFormats;

    if (!supportedFormats.contains("tiff") && !supportedFormats.contains("tif")) {
        qWarning() << "警告: 系统可能不支持TIF格式";
    }
}

void MainWindow::setupLayout()
{
    // 检查centralwidget是否存在
    if (!ui->centralwidget) {
        qWarning() << "警告：未找到centralwidget";
        return;
    }

    // 清理centralwidget的现有布局和控件
    QLayout* existingLayout = ui->centralwidget->layout();
    if (existingLayout) {
        qDebug() << "清理现有布局";
        QLayoutItem* item;
        while ((item = existingLayout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete existingLayout;
    }

    // 创建新的布局
    QVBoxLayout* mainLayout = new QVBoxLayout(ui->centralwidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);

    // 添加图像显示组件到布局
    mainLayout->addWidget(imageDisplayWidget, 1);

    // 设置图片列表的样式（如果存在）
    if (ui->imageListWidget) {
        ui->imageListWidget->setMaximumWidth(250);
        ui->imageListWidget->setMinimumWidth(200);

        ui->imageListWidget->setStyleSheet(
            "QListWidget { "
            "    background-color: #f9f9f9; "
            "    border: 1px solid #ddd; "
            "    border-radius: 3px; "
            "} "
            "QListWidget::item { "
            "    padding: 8px; "
            "    border-bottom: 1px solid #eee; "
            "} "
            "QListWidget::item:selected { "
            "    background-color: #007acc; "
            "    color: white; "
            "} "
            "QListWidget::item:hover { "
            "    background-color: #e6f3ff; "
            "}"
        );
    }

    qDebug() << "布局设置完成";
}

void MainWindow::setupStatusBar()
{
    progressBar = new QProgressBar();
    progressBar->setVisible(false);
    progressBar->setMaximumWidth(300);  // 增加宽度以适应块状显示
    progressBar->setMinimumWidth(200);
    progressBar->setFixedHeight(22);    // 设置合适的高度

    // 使用块状进度条样式
    progressBar->setStyleSheet(
        "QProgressBar{"
        "    text-align:center;"
        "    background-color:#DDDDDD;"
        "    border: 0px solid #DDDDDD;"
        "    border-radius:5px;"
        "    font-size:12px;"
        "    color:#333333;"
        "}"
        "QProgressBar::chunk{"
        "    background-color:#05B8CC;"
        "    border-radius:5px;"
        "    width:8px;"
        "    margin:0.5px;"
        "}"
    );

    statusLabel = new QLabel("就绪");
    statusLabel->setMinimumWidth(150);

    if (ui->statusbar) {
        ui->statusbar->addWidget(statusLabel);
        ui->statusbar->addPermanentWidget(progressBar);
    }
}

void MainWindow::setupConnections()
{
    // 连接菜单动作 - 安全检查每个action是否存在
    if (ui->actionImportPanchromatic) {
        connect(ui->actionImportPanchromatic, &QAction::triggered, this, &MainWindow::onImportPanchromatic);
    }
    if (ui->actionImportMultispectral) {
        connect(ui->actionImportMultispectral, &QAction::triggered, this, &MainWindow::onImportMultispectral);
    }
    if (ui->actionImportBatch) {
        connect(ui->actionImportBatch, &QAction::triggered, this, &MainWindow::onImportBatch);
    }
    if (ui->actionNewProject) {
        connect(ui->actionNewProject, &QAction::triggered, this, &MainWindow::onNewProject);
    }
    if (ui->actionOpenProject) {
        connect(ui->actionOpenProject, &QAction::triggered, this, &MainWindow::onOpenProject);
    }
    if (ui->actionSaveProject) {
        connect(ui->actionSaveProject, &QAction::triggered, this, &MainWindow::onSaveProject);
    }
    if (ui->actionSaveAs) {
        connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::onSaveAs);
    }

    // 导出相关的信号连接（只保留一份）
    if (ui->actionExportRegisteredImage) {
        connect(ui->actionExportRegisteredImage, &QAction::triggered, this, &MainWindow::onExportRegisteredImage);
    }
    if (ui->actionExportFusedImage) {
        connect(ui->actionExportFusedImage, &QAction::triggered, this, &MainWindow::onExportFusedImage);
    }
    if (ui->actionExportDetectionResult) {
        connect(ui->actionExportDetectionResult, &QAction::triggered, this, &MainWindow::onExportDetectionResult);
    }
    if (ui->actionExportWithSettings) {
        connect(ui->actionExportWithSettings, &QAction::triggered, this, &MainWindow::onExportWithSettings);
    }
    if (ui->actionBatchExportSettings) {
        connect(ui->actionBatchExportSettings, &QAction::triggered, this, &MainWindow::onBatchExportSettings);
    }

    // 连接图像列表 - 安全检查
    if (ui->imageListWidget) {
        connect(ui->imageListWidget, &QListWidget::itemClicked, this, &MainWindow::onImageListItemClicked);
    }

    // 连接图像管理器信号
    connect(imageManager, &ImageManager::imageAdded, this, &MainWindow::onImageAdded);
    connect(imageManager, &ImageManager::importProgress, this, &MainWindow::onImportProgress);

    qDebug() << "信号连接设置完成";
}

void MainWindow::showProgress(const QString& message, int value, bool cancelable)
{
    Q_UNUSED(cancelable)

        // 显示在状态栏
        if (statusLabel) {
            statusLabel->setText(message);
        }

    if (progressBar) {
        if (value >= 0) {
            // 确保进度条范围正确
            progressBar->setRange(0, 100);
            progressBar->setValue(value);
            progressBar->setVisible(true);
            // 设置进度条文本
            progressBar->setFormat(QString("%1%").arg(value));

            qDebug() << "Progress updated:" << message << "Value:" << value; // 调试信息
        }
        else {
            // 无限进度模式 - 只在初始时使用
            progressBar->setRange(0, 0);
            progressBar->setVisible(true);
            progressBar->setFormat("加载中...");
        }
    }

    // 强制刷新界面
    QApplication::processEvents();
}

void MainWindow::hideProgress()
{
    if (progressBar) {
        progressBar->setVisible(false);
        progressBar->setRange(0, 100);  // 恢复正常范围
        progressBar->setValue(0);
        progressBar->setFormat("%p%"); // 恢复默认格式
    }
    if (statusLabel) {
        statusLabel->setText("就绪");
    }
}

void MainWindow::showDialogProgress(const QString& message, int value, bool cancelable)
{
    // 使用状态栏进度条代替对话框
    showProgress(message, value, cancelable);
}

void MainWindow::hideDialogProgress(const QString& finishMessage, int delay)
{
    if (!finishMessage.isEmpty()) {
        if (statusLabel) {
            statusLabel->setText(finishMessage);
        }
        if (progressBar) {
            progressBar->setValue(100);
            progressBar->setFormat("完成");
        }

        // 延迟隐藏
        QTimer::singleShot(delay, this, [this]() {
            hideProgress();
            });
    }
    else {
        hideProgress();
    }
}

void MainWindow::updateProgress(const QString& message, int value)
{
    if (!message.isEmpty() && statusLabel) {
        statusLabel->setText(message);
    }
    if (value >= 0 && progressBar) {
        // 确保进度条在正确的范围内
        progressBar->setRange(0, 100);
        progressBar->setValue(value);
        progressBar->setFormat(QString("%1%").arg(value));
        progressBar->setVisible(true);

        qDebug() << "Progress updated via updateProgress:" << message << "Value:" << value; // 调试信息
    }

    // 强制刷新界面
    QApplication::processEvents();
}

// 简化的导入方法
void MainWindow::onImportPanchromatic()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "导入全色影像",
        QString(),
        "图像文件 (*.tif *.tiff *.jpg *.jpeg *.png *.bmp);;TIF文件 (*.tif *.tiff);;所有文件 (*)"
    );

    if (!filePath.isEmpty()) {
        showProgress("正在导入全色影像...", -1);

        if (imageManager->importPanchromaticImage(filePath)) {
            hideProgress();
            ui->statusbar->showMessage("成功导入全色影像: " + QFileInfo(filePath).fileName(), 3000);
        }
        else {
            hideProgress();
            QMessageBox::warning(this, "导入失败", "无法导入该图像文件，请检查文件格式是否支持。");
        }
    }
}

void MainWindow::onImportMultispectral()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "导入多光谱影像",
        QString(),
        "图像文件 (*.tif *.tiff *.jpg *.jpeg *.png *.bmp);;TIF文件 (*.tif *.tiff);;所有文件 (*)"
    );

    if (!filePath.isEmpty()) {
        showProgress("正在导入多光谱影像...", -1);

        if (imageManager->importMultispectralImage(filePath)) {
            hideProgress();
            ui->statusbar->showMessage("成功导入多光谱影像: " + QFileInfo(filePath).fileName(), 3000);
        }
        else {
            hideProgress();
            QMessageBox::warning(this, "导入失败", "无法导入该图像文件，请检查文件格式是否支持。");
        }
    }
}

void MainWindow::onImportBatch()
{
    QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "批量导入图像",
        QString(),
        "图像文件 (*.tif *.tiff *.jpg *.jpeg *.png *.bmp);;TIF文件 (*.tif *.tiff);;所有文件 (*)"
    );

    if (!filePaths.isEmpty()) {
        showProgress(QString("批量导入 %1 个文件...").arg(filePaths.size()), 0);

        if (imageManager->importBatchImages(filePaths)) {
            hideProgress();
            ui->statusbar->showMessage(QString("成功导入 %1 个图像文件").arg(filePaths.size()), 3000);
        }
        else {
            hideProgress();
            QMessageBox::warning(this, "导入完成", "部分或全部图像文件导入失败。");
        }
    }
}

void MainWindow::onNewProject()
{
    if (projectManager->hasCurrentProject()) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "新建工程", "当前工程可能有未保存的更改。是否要先保存？",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) onSaveProject();
    }

    imageManager->clearAllImages();
    imageDisplayWidget->clearDisplay();
    updateImageList();

    QString filePath = QFileDialog::getSaveFileName(this, "创建新工程", QString(), "工程文件 (*.rsp);;所有文件 (*)");

    if (!filePath.isEmpty()) {
        showDialogProgress("正在创建新工程...", 50, false);

        QTimer::singleShot(200, this, [this, filePath]() {
            if (projectManager->newProject(filePath)) {
                updateRecentProjectsMenu();
                hideDialogProgress("新工程创建成功", 1500);
                ui->statusbar->showMessage("新工程创建成功: " + QFileInfo(filePath).fileName(), 3000);
                setWindowTitle("遥感图像处理系统 - " + QFileInfo(filePath).baseName());
            }
            else {
                hideDialogProgress("创建失败", 1000);
                QMessageBox::warning(this, "错误", "无法创建工程文件");
            }
            });
    }
}

void MainWindow::onOpenProject()
{
    if (projectManager->hasCurrentProject()) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "打开工程", "当前工程可能有未保存的更改。是否要先保存？",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) onSaveProject();
    }

    QString filePath = QFileDialog::getOpenFileName(this, "打开工程", QString(), "工程文件 (*.rsp);;所有文件 (*)");

    if (!filePath.isEmpty()) {
        showDialogProgress("正在打开工程...", 30, false);
        imageManager->clearAllImages();
        imageDisplayWidget->clearDisplay();

        QTimer::singleShot(100, this, [this, filePath]() {
            updateProgress("正在解析工程文件...", 60);

            if (projectManager->openProject(filePath)) {
                updateProgress("正在加载图像数据...", 80);
                loadProjectData();
                updateImageList();
                updateRecentProjectsMenu();
                hideDialogProgress("工程打开成功", 1500);
                ui->statusbar->showMessage("工程打开成功: " + QFileInfo(filePath).fileName(), 3000);
                setWindowTitle("遥感图像处理系统 - " + QFileInfo(filePath).baseName());
            }
            else {
                hideDialogProgress("打开失败", 1000);
                QMessageBox::warning(this, "错误", "无法打开工程文件");
            }
            });
    }
}

void MainWindow::onSaveProject()
{
    if (projectManager->getCurrentProjectPath().isEmpty()) {
        onSaveAs();
        return;
    }

    showDialogProgress("正在保存工程...", 50, false);
    saveProjectData();

    QTimer::singleShot(200, this, [this]() {
        if (projectManager->saveProject()) {
            hideDialogProgress("工程保存成功", 1500);
            ui->statusbar->showMessage("工程保存成功", 2000);
        }
        else {
            hideDialogProgress("保存失败", 1000);
            QMessageBox::warning(this, "错误", "工程保存失败");
        }
        });
}

void MainWindow::onSaveAs()
{
    QString filePath = QFileDialog::getSaveFileName(this, "另存为", QString(), "工程文件 (*.rsp);;所有文件 (*)");

    if (!filePath.isEmpty()) {
        showDialogProgress("正在保存工程...", 50, false);
        saveProjectData();

        QTimer::singleShot(200, this, [this, filePath]() {
            if (projectManager->saveAsProject(filePath)) {
                updateRecentProjectsMenu();
                hideDialogProgress("工程保存成功", 1500);
                ui->statusbar->showMessage("工程已保存为: " + QFileInfo(filePath).fileName(), 3000);
                setWindowTitle("遥感图像处理系统 - " + QFileInfo(filePath).baseName());
            }
            else {
                hideDialogProgress("保存失败", 1000);
                QMessageBox::warning(this, "错误", "工程保存失败");
            }
            });
    }
}

void MainWindow::saveProjectData()
{
    QList<ImageInfo> allImages = imageManager->getAllImages();
    projectManager->saveImageList(allImages);

    // 保存当前显示的图像路径
    QString currentDisplayPath = imageDisplayWidget->getCurrentImagePath();
    if (!currentDisplayPath.isEmpty()) {
        projectManager->setCurrentImagePath(currentDisplayPath);
    }

    qDebug() << "工程数据保存完成，包含" << allImages.size() << "个图像";
}

void MainWindow::loadProjectData()
{
    QList<ImageInfo> imageList = projectManager->loadImageList();
    int loadedCount = 0, failedCount = 0;

    for (const ImageInfo& info : imageList) {
        if (imageManager->addImage(info)) {
            loadedCount++;
        }
        else {
            failedCount++;
            qDebug() << "无法加载图像:" << info.filePath;
        }
    }

    qDebug() << QString("工程加载完成：成功 %1 个，失败 %2 个").arg(loadedCount).arg(failedCount);

    if (failedCount > 0) {
        QMessageBox::information(this, "工程加载完成",
            QString("成功加载 %1 个图像。\n%2 个图像文件不存在或无法访问。")
            .arg(loadedCount).arg(failedCount));
    }

    // 恢复上次显示的图像
    QString lastImagePath = projectManager->getCurrentImagePath();
    if (!lastImagePath.isEmpty() && QFileInfo::exists(lastImagePath)) {
        QTimer::singleShot(500, this, [this, lastImagePath]() {
            imageDisplayWidget->displayImage(lastImagePath);
            });
    }
}



void MainWindow::onImageListItemClicked(QListWidgetItem* item)
{
    if (!item) return;

    QString filePath = item->data(Qt::UserRole).toString();
    qDebug() << "点击图像项:" << filePath;

    if (!filePath.isEmpty()) {
        currentImagePath = filePath;
        imageDisplayWidget->displayImage(filePath);
    }
}

void MainWindow::onImageAdded(const ImageInfo& info)
{
    updateImageList();
}



// ImageDisplayWidget 信号的槽函数
void MainWindow::onImageDisplayed(const QString& filePath, int width, int height, int bandCount)
{
    currentImagePath = filePath;
    if (ui->statusbar) {
        ui->statusbar->showMessage(QString("已加载图像: %1 (%2x%3, %4波段)")
            .arg(QFileInfo(filePath).fileName())
            .arg(width).arg(height).arg(bandCount), 5000);
    }
}

void MainWindow::onImageDisplayFailed(const QString& error)
{
    QMessageBox::warning(this, "图像显示失败", error);
    if (ui->statusbar) {
        ui->statusbar->showMessage("图像显示失败", 3000);
    }
}

void MainWindow::onZoomLevelChanged(double level)
{
    if (ui->statusbar) {
        ui->statusbar->showMessage(QString("缩放比例: %1%").arg(static_cast<int>(level * 100)), 2000);
    }
}

// 图像进度更新 - 使用状态栏进度条
void MainWindow::onImageProgressUpdate(const QString& message, int value)
{
    qDebug() << "onImageProgressUpdate called:" << message << "Value:" << value; // 调试信息

    // 确保进度条可见并设置正确的范围
    if (progressBar) {
        if (!progressBar->isVisible()) {
            progressBar->setVisible(true);
        }
        progressBar->setRange(0, 100);
    }

    showProgress(message, value, false);
}

void MainWindow::onImageProgressFinished()
{
    qDebug() << "onImageProgressFinished called"; // 调试信息

    // 显示完成状态
    if (progressBar) {
        progressBar->setRange(0, 100);
        progressBar->setValue(100);
        progressBar->setFormat("完成");
        progressBar->setVisible(true);
    }
    if (statusLabel) {
        statusLabel->setText("图像加载完成");
    }

    // 强制刷新界面
    QApplication::processEvents();

    // 1.5秒后隐藏进度条
    QTimer::singleShot(1500, this, [this]() {
        hideProgress();
        });
}

void MainWindow::onTileLoadProgress(int loaded, int total)
{
    // 瓦片加载进度不再显示，保持界面清洁
    Q_UNUSED(loaded)
        Q_UNUSED(total)
}

void MainWindow::onImportProgress(const QString& message, int percentage)
{
    qDebug() << "onImportProgress called:" << message << "Percentage:" << percentage; // 调试信息

    showProgress(message, percentage, false);

    // 如果是100%，延迟隐藏
    if (percentage >= 100) {
        QTimer::singleShot(1500, this, [this]() {
            hideProgress();
            });
    }
}
// 替换你原来的整个 exportSelectedImages 函数
void MainWindow::exportSelectedImages(const QString& exportType, const QString& defaultPrefix)
{
    QStringList selectedPaths = getSelectedImagePaths();
    int selectedCount = selectedPaths.size();

    if (selectedCount == 0) {
        QMessageBox::information(this, QString("导出%1").arg(exportType),
            QString("请先选择要导出的图像。\n\n您可以在图像列表中勾选需要导出的图像，然后再执行导出操作。"));
        return;
    }

    QList<ImageInfo> selectedImages = getSelectedImages();
    QString imageListText;
    for (int i = 0; i < selectedImages.size() && i < 10; ++i) {
        const ImageInfo& info = selectedImages[i];
        imageListText += QString("\u2022 %1 (%2x%3)\n")
            .arg(info.fileName)
            .arg(info.width)
            .arg(info.height);
    }
    if (selectedImages.size() > 10) {
        imageListText += QString("... 还有 %1 个图像\n").arg(selectedImages.size() - 10);
    }

    QMessageBox confirmDialog(this);
    confirmDialog.setWindowTitle(QString("导出%1").arg(exportType));
    confirmDialog.setIcon(QMessageBox::Question);
    confirmDialog.setText(QString("您选择了 %1 个图像进行导出：").arg(selectedCount));
    confirmDialog.setDetailedText(imageListText);
    confirmDialog.setInformativeText(QString("确定要导出这些图像作为%1吗？").arg(exportType));
    confirmDialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirmDialog.setDefaultButton(QMessageBox::Yes);

    if (confirmDialog.exec() != QMessageBox::Yes) {
        return;
    }

    QString exportDir = QFileDialog::getExistingDirectory(this,
        QString("选择%1导出目录").arg(exportType),
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (exportDir.isEmpty()) {
        return;
    }

    QStringList nameOptions;
    nameOptions << QString("添加前缀：%1原文件名").arg(defaultPrefix);
    nameOptions << "保持原文件名";
    nameOptions << "自定义前缀";

    bool ok = false;
    QString selectedOption = QInputDialog::getItem(this,
        "选择文件命名方式",
        QString("请选择导出文件的命名方式：\n\n示例文件：image.tif\n\n选项1：%1image.tif\n选项2：image.tif\n选项3：自定义前缀+image.tif")
        .arg(defaultPrefix),
        nameOptions, 0, false, &ok);

    if (!ok) {
        return;
    }

    QString prefix = defaultPrefix;
    if (selectedOption == nameOptions[1]) {
        prefix = "";
    }
    else if (selectedOption == nameOptions[2]) {
        prefix = QInputDialog::getText(this, "自定义前缀",
            "请输入文件名前缀（可为空）：\n\n例如：processed_、result_等",
            QLineEdit::Normal, defaultPrefix, &ok);
        if (!ok) {
            return;
        }
    }

    showProgress(QString("正在导出%1...").arg(exportType), 0);

    int successCount = 0;
    int failedCount = 0;
    QStringList failedFiles;
    bool overwriteAll = false;
    bool skipAll = false;

    for (int i = 0; i < selectedPaths.size(); ++i) {
        QString sourcePath = selectedPaths[i];
        QFileInfo sourceInfo(sourcePath);

        int progress = (i * 90) / selectedPaths.size();
        updateProgress(QString("正在导出: %1 (%2/%3)").arg(sourceInfo.fileName()).arg(i + 1).arg(selectedPaths.size()), progress);

        QString exportFileName = generateExportFileName(sourceInfo.fileName(), prefix);
        QString destinationPath = QDir(exportDir).filePath(exportFileName);

        // --- 这是修改的核心逻辑 ---
        // 1. 如果文件存在，并且用户还没有做出“全部覆盖”或“全部跳过”的决定
        if (QFile::exists(destinationPath) && !overwriteAll && !skipAll) {
            QMessageBox overwriteDialog(this);
            overwriteDialog.setWindowTitle("文件已存在");
            overwriteDialog.setIcon(QMessageBox::Question);
            // 这里是之前修复的第一个bug（删除了多余的引号）
            overwriteDialog.setText(QString("文件 %1 已存在。").arg(exportFileName));
            overwriteDialog.setInformativeText("您希望如何处理？");

            QPushButton* overwriteBtn = overwriteDialog.addButton("覆盖", QMessageBox::AcceptRole);
            QPushButton* overwriteAllBtn = overwriteDialog.addButton("全部覆盖", QMessageBox::AcceptRole);
            QPushButton* skipBtn = overwriteDialog.addButton("跳过", QMessageBox::RejectRole);
            QPushButton* skipAllBtn = overwriteDialog.addButton("全部跳过", QMessageBox::RejectRole);
            QPushButton* cancelBtn = overwriteDialog.addButton("取消导出", QMessageBox::RejectRole);

            overwriteDialog.exec();
            QAbstractButton* clickedBtn = overwriteDialog.clickedButton();

            if (clickedBtn == cancelBtn) {
                // 如果用户取消，直接跳出整个循环
                break;
            }
            else if (clickedBtn == skipBtn) {
                // 如果用户选择“跳过”当前文件，记录失败并进入下一次循环
                failedCount++;
                failedFiles.append(QString("%1 (跳过)").arg(sourceInfo.fileName()));
                continue;
            }
            else if (clickedBtn == skipAllBtn) {
                // 如果用户选择“全部跳过”，设置标志位，记录失败并进入下一次循环
                skipAll = true;
                failedCount++;
                failedFiles.append(QString("%1 (跳过)").arg(sourceInfo.fileName()));
                continue;
            }
            else if (clickedBtn == overwriteAllBtn) {
                // 如果用户选择“全部覆盖”，设置标志位，循环将继续执行到下面的复制逻辑
                overwriteAll = true;
            }
            // 如果用户只选择“覆盖”当前文件，则不执行任何操作，循环也会自然地继续执行到下面的复制逻辑
        }

        // 2. 如果 "全部跳过" 标志为真，并且文件存在，则无条件跳过
        if (skipAll && QFile::exists(destinationPath)) {
            failedCount++;
            failedFiles.append(QString("%1 (跳过)").arg(sourceInfo.fileName()));
            continue;
        }

        // 3. 只有在逻辑允许的情况下，才执行复制
        if (copyImageFile(sourcePath, destinationPath)) {
            successCount++;
        }
        else {
            failedCount++;
            failedFiles.append(sourceInfo.fileName());
        }

        QApplication::processEvents();
    }

    hideDialogProgress(QString("%1导出完成").arg(exportType), 1500);

    QString resultMessage = QString("%1导出完成！\n\n").arg(exportType);
    resultMessage += QString("总计: %1 个文件\n").arg(selectedPaths.size());
    resultMessage += QString("成功: %1 个文件\n").arg(successCount);

    // 这里是之前建议的第三个改进点（简化代码）
    if (failedCount > 0) {
        resultMessage += QString("失败/跳过: %1 个文件").arg(failedCount);
        if (!failedFiles.isEmpty()) {
            QStringList detailedFailures = failedFiles.mid(0, 5);
            resultMessage += QString("\n\n失败或跳过的文件:\n%1").arg(detailedFailures.join("\n"));
            if (failedFiles.size() > 5) {
                resultMessage += QString("\n... 还有 %1 个文件").arg(failedFiles.size() - 5);
            }
        }
    }

    resultMessage += QString("\n\n导出目录: %1").arg(exportDir);

    QMessageBox resultDialog(this);
    resultDialog.setWindowTitle(QString("%1导出结果").arg(exportType));

    if (failedCount > 0 && successCount == 0) {
        resultDialog.setIcon(QMessageBox::Critical);
    }
    else if (failedCount > 0) {
        resultDialog.setIcon(QMessageBox::Warning);
    }
    else {
        resultDialog.setIcon(QMessageBox::Information);
    }

    resultDialog.setText(resultMessage);
    resultDialog.setStandardButtons(QMessageBox::Ok);

    QPushButton* openDirBtn = nullptr;
    if (successCount > 0) {
        openDirBtn = resultDialog.addButton("打开导出目录", QMessageBox::ActionRole);
    }

    resultDialog.exec();

    if (resultDialog.clickedButton() == openDirBtn) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(exportDir));
    }

    ui->statusbar->showMessage(QString("%1导出完成 - 成功: %2, 失败: %3")
        .arg(exportType).arg(successCount).arg(failedCount), 5000);
}

bool MainWindow::copyImageFile(const QString& sourcePath, const QString& destinationPath)
{
    // 如果目标文件已存在，先安全地删除它
    if (QFile::exists(destinationPath)) {
        if (!QFile::remove(destinationPath)) {
            qWarning() << "无法删除已存在的文件:" << destinationPath;
            return false;
        }
    }
    // 直接复制
    return QFile(sourcePath).copy(destinationPath);
}

QString MainWindow::generateExportFileName(const QString& originalName, const QString& prefix, const QString& suffix)
{
    QFileInfo info(originalName);
    QString baseName = info.completeBaseName();
    QString extension = info.suffix();

    QString fileName = prefix + baseName;
    if (!suffix.isEmpty())
        fileName += ("_" + suffix);
    fileName += "." + extension;
    return fileName;
}
// *** 实现导出配准影像功能 ***
void MainWindow::onExportRegisteredImage()
{
    qDebug() << "导出配准影像被触发";
    exportSelectedImages("配准影像", "registered_");
}

// *** 实现导出融合影像功能 ***
void MainWindow::onExportFusedImage()
{
    qDebug() << "导出融合影像被触发";
    exportSelectedImages("融合影像", "fused_");
}

// *** 实现导出检测结果功能 ***
void MainWindow::onExportDetectionResult()
{
    qDebug() << "导出检测结果被触发";
    exportSelectedImages("检测结果", "detected_");
}

// *** 增强的自定义导出设置功能 ***
void MainWindow::onExportWithSettings()
{
    QStringList selectedPaths = getSelectedImagePaths();
    if (selectedPaths.isEmpty()) {
        QMessageBox::information(this, "自定义导出设置",
            "请先选择要导出的图像。\n\n您可以在图像列表中勾选需要导出的图像，然后再执行导出操作。");
        return;
    }

    showExportSettingsDialog();
}

// *** 批量导出设置功能 ***
void MainWindow::onBatchExportSettings()
{
    QStringList selectedPaths = getSelectedImagePaths();
    if (selectedPaths.isEmpty()) {
        QMessageBox::information(this, "批量导出设置",
            "请先选择要导出的图像。\n\n您可以在图像列表中勾选需要导出的图像，然后再执行批量导出操作。");
        return;
    }

    // 创建批量导出设置对话框
    QDialog settingsDialog(this);
    settingsDialog.setWindowTitle("批量导出设置");
    settingsDialog.setModal(true);
    settingsDialog.resize(400, 300);

    QVBoxLayout* layout = new QVBoxLayout(&settingsDialog);

    // 显示要导出的图像数量
    QLabel* infoLabel = new QLabel(QString("将要导出 %1 个图像").arg(selectedPaths.size()));
    infoLabel->setStyleSheet("font-weight: bold; color: #2c3e50; margin-bottom: 10px;");
    layout->addWidget(infoLabel);

    // 输出格式选择
    QLabel* formatLabel = new QLabel("输出格式:");
    QComboBox* formatCombo = new QComboBox();
    formatCombo->addItems({ "TIFF (*.tif)", "JPEG (*.jpg)", "PNG (*.png)", "BMP (*.bmp)" });
    formatCombo->setCurrentIndex(0); // 默认选择TIFF
    layout->addWidget(formatLabel);
    layout->addWidget(formatCombo);

    // 文件名前缀
    QLabel* prefixLabel = new QLabel("文件名前缀:");
    QLineEdit* prefixEdit = new QLineEdit("batch_export_");
    layout->addWidget(prefixLabel);
    layout->addWidget(prefixEdit);

    // 保持元数据选项
    QCheckBox* metadataCheckBox = new QCheckBox("保持原始元数据");
    metadataCheckBox->setChecked(true);
    layout->addWidget(metadataCheckBox);

    // 输出目录选择
    QLabel* dirLabel = new QLabel("输出目录:");
    QLineEdit* dirEdit = new QLineEdit();
    QPushButton* browseBtn = new QPushButton("浏览...");

    QHBoxLayout* dirLayout = new QHBoxLayout();
    dirLayout->addWidget(dirEdit);
    dirLayout->addWidget(browseBtn);

    layout->addWidget(dirLabel);
    layout->addLayout(dirLayout);

    // 连接浏览按钮
    connect(browseBtn, &QPushButton::clicked, [this, dirEdit]() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择输出目录");
        if (!dir.isEmpty()) {
            dirEdit->setText(dir);
        }
        });

    // 按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* okBtn = new QPushButton("开始导出");
    QPushButton* cancelBtn = new QPushButton("取消");

    okBtn->setStyleSheet("QPushButton { background-color: #3498db; color: white; padding: 8px 16px; border: none; border-radius: 4px; } QPushButton:hover { background-color: #2980b9; }");
    cancelBtn->setStyleSheet("QPushButton { background-color: #95a5a6; color: white; padding: 8px 16px; border: none; border-radius: 4px; } QPushButton:hover { background-color: #7f8c8d; }");

    buttonLayout->addStretch();
    buttonLayout->addWidget(okBtn);
    buttonLayout->addWidget(cancelBtn);
    layout->addLayout(buttonLayout);

    connect(okBtn, &QPushButton::clicked, &settingsDialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &settingsDialog, &QDialog::reject);

    if (settingsDialog.exec() == QDialog::Accepted) {
        QString exportDir = dirEdit->text().trimmed();
        if (exportDir.isEmpty()) {
            QMessageBox::warning(this, "错误", "请选择输出目录");
            return;
        }

        QString format = formatCombo->currentText().split(" ").first(); // 获取格式名称部分
        QString prefix = prefixEdit->text();
        bool preserveMetadata = metadataCheckBox->isChecked();

        performBatchExportWithSettings(selectedPaths, exportDir, format, prefix, preserveMetadata);
    }
}

// *** 显示高级导出设置对话框 ***
void MainWindow::showExportSettingsDialog()
{
    QStringList selectedPaths = getSelectedImagePaths();

    // 创建自定义导出设置对话框
    QDialog settingsDialog(this);
    settingsDialog.setWindowTitle("自定义导出设置");
    settingsDialog.setModal(true);
    settingsDialog.resize(450, 400);

    QVBoxLayout* layout = new QVBoxLayout(&settingsDialog);

    // 标题
    QLabel* titleLabel = new QLabel("高级导出设置");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #2c3e50; margin-bottom: 15px;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // 显示选中的图像信息
    QLabel* infoLabel = new QLabel(QString("已选择 %1 个图像进行导出").arg(selectedPaths.size()));
    infoLabel->setStyleSheet("color: #34495e; margin-bottom: 10px;");
    layout->addWidget(infoLabel);

    // 导出类型选择
    QGroupBox* typeGroup = new QGroupBox("导出类型");
    QVBoxLayout* typeLayout = new QVBoxLayout(typeGroup);

    QRadioButton* registeredRadio = new QRadioButton("配准影像（添加 'registered_' 前缀）");
    QRadioButton* fusedRadio = new QRadioButton("融合影像（添加 'fused_' 前缀）");
    QRadioButton* detectedRadio = new QRadioButton("检测结果（添加 'detected_' 前缀）");
    QRadioButton* customRadio = new QRadioButton("自定义前缀");

    registeredRadio->setChecked(true); // 默认选择

    typeLayout->addWidget(registeredRadio);
    typeLayout->addWidget(fusedRadio);
    typeLayout->addWidget(detectedRadio);
    typeLayout->addWidget(customRadio);

    layout->addWidget(typeGroup);

    // 自定义前缀输入
    QLabel* customLabel = new QLabel("自定义前缀:");
    QLineEdit* customEdit = new QLineEdit("custom_");
    customEdit->setEnabled(false); // 初始禁用

    layout->addWidget(customLabel);
    layout->addWidget(customEdit);

    // 连接单选按钮，控制自定义输入框
    connect(customRadio, &QRadioButton::toggled, customEdit, &QLineEdit::setEnabled);

    // 输出格式
    QLabel* formatLabel = new QLabel("输出格式:");
    QComboBox* formatCombo = new QComboBox();
    formatCombo->addItems({ "保持原格式", "TIFF (*.tif)", "JPEG (*.jpg)", "PNG (*.png)", "BMP (*.bmp)" });
    layout->addWidget(formatLabel);
    layout->addWidget(formatCombo);

    // 质量设置（仅对JPEG有效）
    QLabel* qualityLabel = new QLabel("JPEG质量 (1-100):");
    QSlider* qualitySlider = new QSlider(Qt::Horizontal);
    qualitySlider->setRange(1, 100);
    qualitySlider->setValue(95);
    qualitySlider->setEnabled(false);

    QLabel* qualityValueLabel = new QLabel("95");
    connect(qualitySlider, &QSlider::valueChanged, qualityValueLabel, [qualityValueLabel](int value) {
        qualityValueLabel->setText(QString::number(value));
        });

    QHBoxLayout* qualityLayout = new QHBoxLayout();
    qualityLayout->addWidget(qualitySlider);
    qualityLayout->addWidget(qualityValueLabel);

    layout->addWidget(qualityLabel);
    layout->addLayout(qualityLayout);

    // 当选择JPEG格式时启用质量设置
    connect(formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [qualityLabel, qualitySlider](int index) {
        bool isJpeg = (index == 2); // JPEG在索引2
        qualityLabel->setEnabled(isJpeg);
        qualitySlider->setEnabled(isJpeg);
        });

    // 按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* okBtn = new QPushButton("开始导出");
    QPushButton* cancelBtn = new QPushButton("取消");

    okBtn->setStyleSheet("QPushButton { background-color: #27ae60; color: white; padding: 10px 20px; border: none; border-radius: 5px; font-weight: bold; } QPushButton:hover { background-color: #219a52; }");
    cancelBtn->setStyleSheet("QPushButton { background-color: #e74c3c; color: white; padding: 10px 20px; border: none; border-radius: 5px; font-weight: bold; } QPushButton:hover { background-color: #c0392b; }");

    buttonLayout->addStretch();
    buttonLayout->addWidget(okBtn);
    buttonLayout->addWidget(cancelBtn);
    layout->addLayout(buttonLayout);

    connect(okBtn, &QPushButton::clicked, &settingsDialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &settingsDialog, &QDialog::reject);

    if (settingsDialog.exec() == QDialog::Accepted) {
        // 确定导出类型和前缀
        QString exportType;
        QString prefix;

        if (registeredRadio->isChecked()) {
            exportType = "配准影像";
            prefix = "registered_";
        }
        else if (fusedRadio->isChecked()) {
            exportType = "融合影像";
            prefix = "fused_";
        }
        else if (detectedRadio->isChecked()) {
            exportType = "检测结果";
            prefix = "detected_";
        }
        else if (customRadio->isChecked()) {
            exportType = "自定义导出";
            prefix = customEdit->text();
        }

        // 执行导出
        exportSelectedImages(exportType, prefix);
    }
}

// *** 执行带设置的批量导出 ***
void MainWindow::performBatchExportWithSettings(const QStringList& selectedPaths, const QString& exportDir,
    const QString& format, const QString& prefix, bool preserveMetadata)
{
    Q_UNUSED(preserveMetadata) // 暂时未使用元数据保持功能

        showProgress("正在执行批量导出...", 0);

    int successCount = 0;
    int failedCount = 0;
    QStringList failedFiles;

    for (int i = 0; i < selectedPaths.size(); ++i) {
        QString sourcePath = selectedPaths[i];
        QFileInfo sourceInfo(sourcePath);

        int progress = (i * 90) / selectedPaths.size();
        updateProgress(QString("正在导出: %1 (%2/%3)").arg(sourceInfo.fileName()).arg(i + 1).arg(selectedPaths.size()), progress);

        // 生成导出文件名
        QString exportFileName = generateExportFileName(sourceInfo.fileName(), prefix);

        // 如果指定了格式，修改扩展名
        if (format != "保持原格式") {
            QString newExt;
            if (format == "TIFF") newExt = "tif";
            else if (format == "JPEG") newExt = "jpg";
            else if (format == "PNG") newExt = "png";
            else if (format == "BMP") newExt = "bmp";

            if (!newExt.isEmpty()) {
                QFileInfo exportInfo(exportFileName);
                exportFileName = exportInfo.completeBaseName() + "." + newExt;
            }
        }

        QString destinationPath = QDir(exportDir).filePath(exportFileName);

        // 执行复制
        if (copyImageFile(sourcePath, destinationPath)) {
            successCount++;
        }
        else {
            failedCount++;
            failedFiles.append(sourceInfo.fileName());
        }

        QApplication::processEvents();
    }

    hideDialogProgress("批量导出完成", 1500);

    // 显示结果
    QString resultMessage = QString("批量导出完成！\n\n");
    resultMessage += QString("总计: %1 个文件\n").arg(selectedPaths.size());
    resultMessage += QString("成功: %1 个文件\n").arg(successCount);

    if (failedCount > 0) {
        resultMessage += QString("失败: %1 个文件\n").arg(failedCount);
        if (!failedFiles.isEmpty()) {
            resultMessage += QString("\n失败的文件:\n%1").arg(failedFiles.join("\n"));
        }
    }

    resultMessage += QString("\n导出目录: %1").arg(exportDir);

    QMessageBox::information(this, "批量导出结果", resultMessage);

    // 询问是否打开导出目录
    if (successCount > 0) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "导出完成",
            "是否要打开导出目录查看结果？",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(exportDir));
        }
    }

    ui->statusbar->showMessage(QString("批量导出完成 - 成功: %1, 失败: %2")
        .arg(successCount).arg(failedCount), 5000);
}
void MainWindow::setupGeometricRegistrationConnections()
{
    // 连接几何配准菜单的信号槽
    connect(ui->actionManualRegistration, &QAction::triggered,
        this, &MainWindow::onManualRegistration);
    connect(ui->actionControlPointManager, &QAction::triggered,
        this, &MainWindow::onControlPointManager);
    connect(ui->actionRegistrationSettings, &QAction::triggered,
        this, &MainWindow::onRegistrationSettings);
}



void MainWindow::onControlPointManager()
{
    // 如果配准窗口已打开，激活它
    if (registrationWindow && registrationWindow->isVisible()) {
        registrationWindow->raise();
        registrationWindow->activateWindow();
        QMessageBox::information(this, "控制点管理",
            "配准窗口已打开，您可以在其中管理控制点。");
        return;
    }

    // 否则打开配准窗口
    onManualRegistration();
}

// 修复手动配准方法
void MainWindow::onManualRegistration()
{
    // 获取选中的图像
    QStringList selectedPaths = getSelectedImagePaths();

    if (selectedPaths.isEmpty()) {
        QMessageBox::information(this, "几何配准",
            "请先在图像列表中选择要配准的图像。\n\n"
            "建议选择一个基准影像和一个待配准影像。");
        return;
    }

    // 如果配准窗口已存在，安全关闭
    if (registrationWindow) {
        // 先断开所有信号连接，防止析构时的信号问题
        disconnect(registrationWindow, nullptr, this, nullptr);

        // 关闭并等待窗口完全关闭
        registrationWindow->close();

        // 直接删除，避免deleteLater的异步问题
        delete registrationWindow;
        registrationWindow = nullptr;

        // 给Qt事件循环一些时间处理
        QApplication::processEvents();
    }

    // 创建新的配准窗口
    try {
        registrationWindow = new GeometricRegistrationWindow(imageManager, this);

        // 连接信号（在对象完全创建后）
        connect(registrationWindow, &GeometricRegistrationWindow::registrationCompleted,
            this, &MainWindow::onRegistrationCompleted, Qt::QueuedConnection);
        connect(registrationWindow, &GeometricRegistrationWindow::windowClosed,
            this, &MainWindow::onRegistrationCanceled, Qt::QueuedConnection);

        // 智能分配图像
        if (selectedPaths.size() == 1) {
            registrationWindow->setTargetImage(selectedPaths[0]);
            QMessageBox::information(this, "配准设置",
                QString("已设置待配准影像：%1\n\n请在配准窗口中选择参考影像。")
                .arg(QFileInfo(selectedPaths[0]).fileName()));
        }
        else if (selectedPaths.size() >= 2) {
            QList<ImageInfo> selectedImages = getSelectedImages();
            QString baseImagePath, targetImagePath;
            bool hasBase = false, hasTarget = false;

            for (const ImageInfo& info : selectedImages) {
                if (info.type == ImageType::Panchromatic && !hasBase) {
                    baseImagePath = info.filePath;
                    hasBase = true;
                }
                else if (info.type == ImageType::Multispectral && !hasTarget) {
                    targetImagePath = info.filePath;
                    hasTarget = true;
                }
            }

            if (!hasBase || !hasTarget) {
                baseImagePath = selectedPaths[0];
                targetImagePath = selectedPaths[1];
            }

            registrationWindow->setReferenceImage(baseImagePath);
            registrationWindow->setTargetImage(targetImagePath);

            QMessageBox::information(this, "配准设置",
                QString("已自动设置：\n"
                    "参考影像：%1\n"
                    "待配准影像：%2")
                .arg(QFileInfo(baseImagePath).fileName())
                .arg(QFileInfo(targetImagePath).fileName()));
        }

        // 显示配准窗口
        registrationWindow->show();
        registrationWindow->raise();
        registrationWindow->activateWindow();

        ui->statusbar->showMessage("几何配准窗口已打开", 3000);
    }
    catch (const std::exception& e) {
        QMessageBox::critical(this, "错误",
            QString("创建配准窗口时发生错误: %1").arg(e.what()));
        if (registrationWindow) {
            delete registrationWindow;
            registrationWindow = nullptr;
        }
    }
    catch (...) {
        QMessageBox::critical(this, "错误", "创建配准窗口时发生未知错误");
        if (registrationWindow) {
            delete registrationWindow;
            registrationWindow = nullptr;
        }
    }
}

// 修复配准完成处理
void MainWindow::onRegistrationCompleted(const QString& resultPath)
{
    // 使用QueuedConnection确保安全调用
    QTimer::singleShot(0, this, [this, resultPath]() {
        ui->statusbar->showMessage("几何配准已完成", 5000);

        if (!resultPath.isEmpty() && QFileInfo::exists(resultPath)) {
            if (imageManager->importPanchromaticImage(resultPath)) {
                updateImageList();
                ui->statusbar->showMessage("配准结果已添加到图像列表", 3000);
            }
        }
        });
}

// 修复配准取消处理
void MainWindow::onRegistrationCanceled()
{
    // 使用QueuedConnection确保安全调用
    QTimer::singleShot(0, this, [this]() {
        ui->statusbar->showMessage("几何配准窗口已关闭", 3000);

        // 安全清理配准窗口引用
        if (registrationWindow) {
            // 断开信号连接
            disconnect(registrationWindow, nullptr, this, nullptr);
            registrationWindow = nullptr;  // 只清空指针，窗口会自动析构
        }
        });
}


/*
* std::string panPath = "nad.tif";
    std::string msPath = "mux.tif";
    std::string outputPath = "fused_PCA_improved.tif";

    if (ImprovedPCAFusion::performPCAFusion(panPath, msPath, outputPath, 0.1)) {
        std::cout << "\n程序执行成功!" << std::endl;
    }
    else {
        std::cerr << "\n程序执行失败!" << std::endl;
        return -1;
    }
*/
void MainWindow::porcessHis(const QString& img1, const QString& img2, const QString& output)
{
    qDebug() << "=== IHS融合开始 ===";
    qDebug() << "多光谱影像:" << img1;
    qDebug() << "全色影像:" << img2;
    qDebug() << "输出路径:" << output;

    try {
        // 检查GDAL初始化
        qDebug() << "检查GDAL驱动数量:" << GDALGetDriverCount();

        std::string msPath = img1.toStdString();
        std::string panPath = img2.toStdString();
        std::string outputPath = output.toStdString();

        qDebug() << "转换为std::string完成";
        qDebug() << "调用 MemoryOptimizedFusion::performTiledFusion";

        // 记录开始时间
        QTime startTime = QTime::currentTime();

        bool result = MemoryOptimizedFusion::performTiledFusion(msPath, panPath, outputPath);

        QTime endTime = QTime::currentTime();
        qDebug() << "IHS融合耗时:" << startTime.msecsTo(endTime) << "毫秒";
        qDebug() << "IHS融合返回结果:" << result;

        if (result) {
            qDebug() << "IHS融合程序执行成功!";

            // 检查输出文件
            QFileInfo outputInfo(output);
            qDebug() << "输出文件存在:" << outputInfo.exists();
            if (outputInfo.exists()) {
                qDebug() << "输出文件大小:" << outputInfo.size() << "bytes";
            }
        }
        else {
            qDebug() << "IHS融合程序执行失败!";
            throw std::runtime_error("IHS融合执行失败");
        }
    }
    catch (const std::exception& e) {
        QString errorMsg = QString("IHS融合异常: %1").arg(e.what());
        qDebug() << errorMsg;
        std::cerr << errorMsg.toStdString() << std::endl;
        throw;
    }
    catch (...) {
        QString errorMsg = "IHS融合发生未知异常";
        qDebug() << errorMsg;
        std::cerr << errorMsg.toStdString() << std::endl;
        throw std::runtime_error("IHS融合发生未知异常");
    }

    qDebug() << "=== IHS融合结束 ===";
}
void MainWindow::porcessPCA(const QString& img1, const QString& img2, const QString& output)
{
    qDebug() << "=== PCA融合详细调试开始 ===";
    qDebug() << "多光谱影像:" << img1;
    qDebug() << "全色影像:" << img2;
    qDebug() << "输出路径:" << output;

    // ✅ 1. 详细的输入验证
    QFileInfo msFile(img1);
    QFileInfo panFile(img2);
    QFileInfo outputFile(output);

    qDebug() << "=== 输入文件验证 ===";
    qDebug() << "多光谱文件:";
    qDebug() << "  - 存在:" << msFile.exists();
    qDebug() << "  - 可读:" << msFile.isReadable();
    qDebug() << "  - 大小:" << msFile.size() << "bytes";
    qDebug() << "  - 绝对路径:" << msFile.absoluteFilePath();

    qDebug() << "全色文件:";
    qDebug() << "  - 存在:" << panFile.exists();
    qDebug() << "  - 可读:" << panFile.isReadable();
    qDebug() << "  - 大小:" << panFile.size() << "bytes";
    qDebug() << "  - 绝对路径:" << panFile.absoluteFilePath();

    qDebug() << "输出文件:";
    qDebug() << "  - 输出目录存在:" << outputFile.dir().exists();
    qDebug() << "  - 输出目录可写:" << QFileInfo(outputFile.dir().absolutePath()).isWritable();
    qDebug() << "  - 输出绝对路径:" << outputFile.absoluteFilePath();

    // ✅ 2. 检查文件是否能被GDAL识别
    qDebug() << "=== GDAL文件格式检查 ===";
    try {
        qDebug() << "GDAL驱动数量:" << GDALGetDriverCount();

        // 测试打开文件
        GDALDataset* testMsDS = (GDALDataset*)GDALOpen(img1.toStdString().c_str(), GA_ReadOnly);
        if (testMsDS) {
            qDebug() << "多光谱影像GDAL测试: 成功";
            qDebug() << "  - 尺寸:" << testMsDS->GetRasterXSize() << "x" << testMsDS->GetRasterYSize();
            qDebug() << "  - 波段数:" << testMsDS->GetRasterCount();
            qDebug() << "  - 驱动:" << testMsDS->GetDriver()->GetDescription();
            GDALClose(testMsDS);
        }
        else {
            qDebug() << "多光谱影像GDAL测试: 失败";
            qDebug() << "GDAL错误:" << CPLGetLastErrorMsg();
        }

        GDALDataset* testPanDS = (GDALDataset*)GDALOpen(img2.toStdString().c_str(), GA_ReadOnly);
        if (testPanDS) {
            qDebug() << "全色影像GDAL测试: 成功";
            qDebug() << "  - 尺寸:" << testPanDS->GetRasterXSize() << "x" << testPanDS->GetRasterYSize();
            qDebug() << "  - 波段数:" << testPanDS->GetRasterCount();
            qDebug() << "  - 驱动:" << testPanDS->GetDriver()->GetDescription();
            GDALClose(testPanDS);
        }
        else {
            qDebug() << "全色影像GDAL测试: 失败";
            qDebug() << "GDAL错误:" << CPLGetLastErrorMsg();
        }

    }
    catch (...) {
        qDebug() << "GDAL文件检查异常";
    }

    // ✅ 3. 内存检查（估算）
    qDebug() << "=== 内存需求估算 ===";
    if (msFile.exists() && panFile.exists()) {
        long long totalSize = msFile.size() + panFile.size();
        long long estimatedMemoryMB = totalSize / (1024 * 1024) * 3; // 估算需要3倍文件大小的内存
        qDebug() << "预估内存需求:" << estimatedMemoryMB << "MB";

        if (estimatedMemoryMB > 2000) {
            qDebug() << "警告: 内存需求较大，可能需要较长处理时间";
        }
    }

    try {
        std::string msPath = img1.toStdString();
        std::string panPath = img2.toStdString();
        std::string outputPath = output.toStdString();

        qDebug() << "=== 转换为std::string完成 ===";
        qDebug() << "MS path length:" << msPath.length();
        qDebug() << "PAN path length:" << panPath.length();
        qDebug() << "Output path length:" << outputPath.length();

        // ✅ 4. 检查路径中的特殊字符
        QString msPathCheck = QString::fromStdString(msPath);
        QString panPathCheck = QString::fromStdString(panPath);
        // 新代码
        QRegularExpression nonAsciiRegex("[^\\x00-\\x7F]");
        if (msPathCheck.contains(nonAsciiRegex)) {
            qDebug() << "警告: 多光谱路径包含非ASCII字符";
        }
        if (panPathCheck.contains(nonAsciiRegex)) {
            qDebug() << "警告: 全色路径包含非ASCII字符";
        }

        qDebug() << "=== 调用 ImprovedPCAFusion::performPCAFusion ===";
        qDebug() << "参数顺序: panPath, msPath, outputPath, 0.1";

        // ✅ 5. 记录详细时间
        QTime startTime = QTime::currentTime();
        qDebug() << "开始时间:" << startTime.toString("hh:mm:ss.zzz");

        // ✅ 6. 在调用前清除GDAL错误状态
        CPLErrorReset();

        bool result = ImprovedPCAFusion::performPCAFusion(panPath, msPath, outputPath, 0.1);

        QTime endTime = QTime::currentTime();
        int elapsedMs = startTime.msecsTo(endTime);
        qDebug() << "结束时间:" << endTime.toString("hh:mm:ss.zzz");
        qDebug() << "PCA融合耗时:" << elapsedMs << "毫秒 (" << (elapsedMs / 1000.0) << "秒)";
        qDebug() << "PCA融合返回结果:" << result;

        // ✅ 7. 详细的输出文件检查
        qDebug() << "=== 输出结果验证 ===";
        QFileInfo resultInfo(output);
        qDebug() << "输出文件存在:" << resultInfo.exists();

        if (resultInfo.exists()) {
            qDebug() << "输出文件大小:" << resultInfo.size() << "bytes";
            qDebug() << "输出文件可读:" << resultInfo.isReadable();
            qDebug() << "输出文件修改时间:" << resultInfo.lastModified().toString();

            // 尝试用GDAL打开输出文件验证
            GDALDataset* resultDS = (GDALDataset*)GDALOpen(output.toStdString().c_str(), GA_ReadOnly);
            if (resultDS) {
                qDebug() << "输出文件GDAL验证: 成功";
                qDebug() << "  - 输出尺寸:" << resultDS->GetRasterXSize() << "x" << resultDS->GetRasterYSize();
                qDebug() << "  - 输出波段数:" << resultDS->GetRasterCount();
                GDALClose(resultDS);
            }
            else {
                qDebug() << "输出文件GDAL验证: 失败";
                qDebug() << "GDAL错误:" << CPLGetLastErrorMsg();
            }
        }
        else {
            qDebug() << "输出文件不存在！检查错误信息...";
            qDebug() << "最后的GDAL错误:" << CPLGetLastErrorMsg();
        }

        if (result) {
            qDebug() << "✅ PCA融合程序执行成功!";
        }
        else {
            qDebug() << "❌ PCA融合程序执行失败!";
            qDebug() << "最终GDAL错误状态:" << CPLGetLastErrorMsg();
            throw std::runtime_error("PCA融合执行失败");
        }

    }
    catch (const std::exception& e) {
        QString errorMsg = QString("PCA融合异常: %1").arg(e.what());
        qDebug() << "❌" << errorMsg;
        qDebug() << "异常时的GDAL错误:" << CPLGetLastErrorMsg();
        std::cerr << errorMsg.toStdString() << std::endl;
        throw;
    }
    catch (...) {
        QString errorMsg = "PCA融合发生未知异常";
        qDebug() << "❌" << errorMsg;
        qDebug() << "未知异常时的GDAL错误:" << CPLGetLastErrorMsg();
        std::cerr << errorMsg.toStdString() << std::endl;
        throw std::runtime_error("PCA融合发生未知异常");
    }

    qDebug() << "=== PCA融合详细调试结束 ===";
}
void MainWindow::onRegistrationSettings()
{
    qDebug() << "MainWindow::onRegistrationSettings 被调用";

    // 创建配准设置对话框
    QDialog settingsDialog(this);
    settingsDialog.setWindowTitle("配准参数设置");
    settingsDialog.setModal(true);
    settingsDialog.resize(450, 400);

    QVBoxLayout* layout = new QVBoxLayout(&settingsDialog);
    layout->setSpacing(15);
    layout->setContentsMargins(20, 20, 20, 20);

    // 标题
    QLabel* titleLabel = new QLabel("几何配准参数配置");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #2c3e50; margin-bottom: 15px;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // 配准方法选择
    QGroupBox* methodGroup = new QGroupBox("配准方法");
    methodGroup->setStyleSheet("QGroupBox { font-weight: bold; padding-top: 10px; } QGroupBox::title { subcontrol-origin: margin; padding: 0 5px; }");
    QVBoxLayout* methodLayout = new QVBoxLayout(methodGroup);

    QRadioButton* linearRadio = new QRadioButton("线性变换（至少3个控制点）");
    QRadioButton* polynomialRadio = new QRadioButton("多项式变换（推荐6个以上控制点）");
    QRadioButton* splineRadio = new QRadioButton("样条函数变换（适用于复杂变形）");

    linearRadio->setChecked(true); // 默认选择线性变换

    methodLayout->addWidget(linearRadio);
    methodLayout->addWidget(polynomialRadio);
    methodLayout->addWidget(splineRadio);
    layout->addWidget(methodGroup);

    // 多项式阶数设置
    QGroupBox* orderGroup = new QGroupBox("多项式阶数");
    orderGroup->setStyleSheet("QGroupBox { font-weight: bold; padding-top: 10px; } QGroupBox::title { subcontrol-origin: margin; padding: 0 5px; }");
    QHBoxLayout* orderLayout = new QHBoxLayout(orderGroup);

    QLabel* orderLabel = new QLabel("阶数:");
    QSpinBox* orderSpinBox = new QSpinBox();
    orderSpinBox->setRange(1, 5);
    orderSpinBox->setValue(2);
    orderSpinBox->setEnabled(false); // 初始禁用

    orderLayout->addWidget(orderLabel);
    orderLayout->addWidget(orderSpinBox);
    orderLayout->addStretch();
    layout->addWidget(orderGroup);

    // 精度设置
    QGroupBox* accuracyGroup = new QGroupBox("精度设置");
    accuracyGroup->setStyleSheet("QGroupBox { font-weight: bold; padding-top: 10px; } QGroupBox::title { subcontrol-origin: margin; padding: 0 5px; }");
    QFormLayout* accuracyLayout = new QFormLayout(accuracyGroup);

    QDoubleSpinBox* toleranceSpinBox = new QDoubleSpinBox();
    toleranceSpinBox->setRange(0.1, 5.0);
    toleranceSpinBox->setValue(0.5);
    toleranceSpinBox->setSuffix(" 像素");
    toleranceSpinBox->setDecimals(2);

    QCheckBox* weightedCheckBox = new QCheckBox("使用加权最小二乘法");
    weightedCheckBox->setChecked(true);

    accuracyLayout->addRow("误差阈值:", toleranceSpinBox);
    accuracyLayout->addRow("", weightedCheckBox);
    layout->addWidget(accuracyGroup);

    // 连接多项式选项
    connect(polynomialRadio, &QRadioButton::toggled, orderSpinBox, &QSpinBox::setEnabled);

    // 按钮布局
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* okBtn = new QPushButton("确定");
    QPushButton* cancelBtn = new QPushButton("取消");
    QPushButton* defaultBtn = new QPushButton("恢复默认");

    // 按钮样式
    okBtn->setStyleSheet("QPushButton { background-color: #3498db; color: white; padding: 8px 16px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #2980b9; }");
    cancelBtn->setStyleSheet("QPushButton { background-color: #95a5a6; color: white; padding: 8px 16px; border: none; border-radius: 4px; } QPushButton:hover { background-color: #7f8c8d; }");
    defaultBtn->setStyleSheet("QPushButton { background-color: #f39c12; color: white; padding: 8px 16px; border: none; border-radius: 4px; } QPushButton:hover { background-color: #e67e22; }");

    buttonLayout->addWidget(defaultBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okBtn);
    buttonLayout->addWidget(cancelBtn);
    layout->addLayout(buttonLayout);

    // 连接按钮信号
    connect(okBtn, &QPushButton::clicked, &settingsDialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &settingsDialog, &QDialog::reject);
    connect(defaultBtn, &QPushButton::clicked, [&]() {
        linearRadio->setChecked(true);
        orderSpinBox->setValue(2);
        toleranceSpinBox->setValue(0.5);
        weightedCheckBox->setChecked(true);
        });

    // 显示对话框并处理结果
    if (settingsDialog.exec() == QDialog::Accepted) {
        // 保存设置
        QString method = linearRadio->isChecked() ? "线性" :
            polynomialRadio->isChecked() ? "多项式" : "样条";

        QString settingsInfo = QString("配准参数已更新：\n\n"
            "方法：%1\n"
            "多项式阶数：%2\n"
            "误差阈值：%3 像素\n"
            "加权最小二乘：%4")
            .arg(method)
            .arg(orderSpinBox->value())
            .arg(toleranceSpinBox->value(), 0, 'f', 2)
            .arg(weightedCheckBox->isChecked() ? "是" : "否");

        QMessageBox::information(this, "设置保存", settingsInfo);

        if (ui->statusbar) {
            ui->statusbar->showMessage("配准参数已更新", 3000);
        }

        qDebug() << "配准设置已保存：" << method;
    }
}
void MainWindow::setupImageFusionConnections()
{
    // 连接影像融合菜单的信号槽
    if (ui->actionImageFusionSettings) {
        connect(ui->actionImageFusionSettings, &QAction::triggered,
            this, &MainWindow::onImageFusionSettings);
    }
    if (ui->actionPCAFusion) {
        connect(ui->actionPCAFusion, &QAction::triggered,
            this, &MainWindow::onPCAFusion);
    }
    if (ui->actionIHSFusion) {
        connect(ui->actionIHSFusion, &QAction::triggered,
            this, &MainWindow::onIHSFusion);
    }

    qDebug() << "影像融合信号连接完成";
}

void MainWindow::onImageFusionSettings()
{
    qDebug() << "影像融合设置被触发";
    showFusionDialog();
}

void MainWindow::onPCAFusion()
{
    qDebug() << "PCA融合被触发";
    FusionParameters params;
    params.method = "PCA";
    showFusionDialog();
}

void MainWindow::onIHSFusion()
{
    qDebug() << "IHS融合被触发";
    FusionParameters params;
    params.method = "IHS";
    showFusionDialog();
}

void MainWindow::showFusionDialog()
{
    QList<ImageInfo> allImages = imageManager->getAllImages();

    if (allImages.size() < 2) {
        QMessageBox::information(this, "影像融合",
            "影像融合需要至少2张图像。\n\n"
            "请先导入全色影像和多光谱影像，然后再执行融合操作。\n\n"
            "建议：\n"
            "• 导入1张全色影像（高分辨率，单波段）\n"
            "• 导入1张多光谱影像（较低分辨率，多波段）");
        return;
    }

    // 创建融合设置对话框
    QDialog fusionDialog(this);
    fusionDialog.setWindowTitle("影像融合设置");
    fusionDialog.setModal(true);
    fusionDialog.resize(500, 450);

    QVBoxLayout* layout = new QVBoxLayout(&fusionDialog);
    layout->setSpacing(15);
    layout->setContentsMargins(20, 20, 20, 20);

    // 标题
    QLabel* titleLabel = new QLabel("影像融合参数设置");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #2c3e50; margin-bottom: 15px;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // 图像选择组
    QGroupBox* imageGroup = new QGroupBox("图像选择");
    QFormLayout* imageLayout = new QFormLayout(imageGroup);

    QComboBox* panImageCombo = new QComboBox();
    QComboBox* msImageCombo = new QComboBox();

    // 填充图像列表
    panImageCombo->addItem("请选择全色影像", "");
    msImageCombo->addItem("请选择多光谱影像", "");

    for (const ImageInfo& info : allImages) {
        QString displayText = QString("%1 (%2x%3, %4波段)")
            .arg(info.fileName)
            .arg(info.width)
            .arg(info.height)
            .arg(info.bandCount);

        panImageCombo->addItem(displayText, info.filePath);
        msImageCombo->addItem(displayText, info.filePath);
    }

    imageLayout->addRow("全色影像:", panImageCombo);
    imageLayout->addRow("多光谱影像:", msImageCombo);
    layout->addWidget(imageGroup);

    // 输出设置组
    QGroupBox* outputGroup = new QGroupBox("输出设置");
    QVBoxLayout* outputLayout = new QVBoxLayout(outputGroup);

    QHBoxLayout* pathLayout = new QHBoxLayout();
    QLineEdit* outputPathEdit = new QLineEdit();
    QPushButton* browseButton = new QPushButton("浏览...");
    browseButton->setFixedWidth(80);

    pathLayout->addWidget(new QLabel("输出路径:"));
    pathLayout->addWidget(outputPathEdit);
    pathLayout->addWidget(browseButton);

    outputLayout->addLayout(pathLayout);
    layout->addWidget(outputGroup);

    // 融合方法组
    QGroupBox* methodGroup = new QGroupBox("融合方法");
    QVBoxLayout* methodLayout = new QVBoxLayout(methodGroup);

    QRadioButton* pcaRadio = new QRadioButton("PCA融合（主成分分析融合）");
    QRadioButton* ihsRadio = new QRadioButton("IHS融合（强度-色调-饱和度融合）");
    pcaRadio->setChecked(true);

    methodLayout->addWidget(pcaRadio);
    methodLayout->addWidget(ihsRadio);
    layout->addWidget(methodGroup);

    // 参数设置组
    QGroupBox* paramGroup = new QGroupBox("参数设置");
    QFormLayout* paramLayout = new QFormLayout(paramGroup);

    QDoubleSpinBox* sampleRatioSpinBox = new QDoubleSpinBox();
    sampleRatioSpinBox->setRange(0.01, 1.0);
    sampleRatioSpinBox->setValue(0.1);
    sampleRatioSpinBox->setSingleStep(0.01);
    sampleRatioSpinBox->setDecimals(2);

    paramLayout->addRow("采样比例:", sampleRatioSpinBox);
    layout->addWidget(paramGroup);

    // 按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* executeButton = new QPushButton("执行融合");
    QPushButton* cancelButton = new QPushButton("取消");

    executeButton->setStyleSheet("QPushButton { background-color: #3498db; color: white; padding: 10px 20px; border: none; border-radius: 5px; font-weight: bold; } QPushButton:hover { background-color: #2980b9; }");
    cancelButton->setStyleSheet("QPushButton { background-color: #95a5a6; color: white; padding: 10px 20px; border: none; border-radius: 5px; } QPushButton:hover { background-color: #7f8c8d; }");

    buttonLayout->addStretch();
    buttonLayout->addWidget(executeButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    // 连接浏览按钮
    connect(browseButton, &QPushButton::clicked, [this, outputPathEdit]() {
        QString fileName = QFileDialog::getSaveFileName(this,
            "选择输出文件路径",
            QString(),
            "TIFF文件 (*.tif *.tiff);;所有文件 (*)");
        if (!fileName.isEmpty()) {
            outputPathEdit->setText(fileName);
        }
        });

    // 连接执行按钮
    connect(executeButton, &QPushButton::clicked, [&]() {
        FusionParameters params;
        params.panPath = panImageCombo->currentData().toString();
        params.msPath = msImageCombo->currentData().toString();
        params.outputPath = outputPathEdit->text().trimmed();
        params.method = pcaRadio->isChecked() ? "PCA" : "IHS";
        params.sampleRatio = sampleRatioSpinBox->value();

        if (validateFusionInputs(params)) {
            fusionDialog.accept();
            executeFusion(params);
        }
        });

    connect(cancelButton, &QPushButton::clicked, &fusionDialog, &QDialog::reject);

    // 显示对话框
    fusionDialog.exec();
}

bool MainWindow::validateFusionInputs(const FusionParameters& params)
{
    qDebug() << "=== 融合参数验证 ===";

    if (params.panPath.isEmpty()) {
        qDebug() << "验证失败: 全色影像路径为空";
        QMessageBox::warning(this, "输入错误", "请选择全色影像");
        return false;
    }

    if (params.msPath.isEmpty()) {
        qDebug() << "验证失败: 多光谱影像路径为空";
        QMessageBox::warning(this, "输入错误", "请选择多光谱影像");
        return false;
    }

    if (params.outputPath.isEmpty()) {
        qDebug() << "验证失败: 输出路径为空";
        QMessageBox::warning(this, "输入错误", "请设置输出路径");
        return false;
    }

    if (params.panPath == params.msPath) {
        qDebug() << "验证失败: 全色影像和多光谱影像是同一个文件";
        QMessageBox::warning(this, "输入错误", "全色影像和多光谱影像不能是同一个文件");
        return false;
    }

    if (!QFileInfo::exists(params.panPath)) {
        qDebug() << "验证失败: 全色影像文件不存在:" << params.panPath;
        QMessageBox::warning(this, "输入错误", "全色影像文件不存在: " + params.panPath);
        return false;
    }

    if (!QFileInfo::exists(params.msPath)) {
        qDebug() << "验证失败: 多光谱影像文件不存在:" << params.msPath;
        QMessageBox::warning(this, "输入错误", "多光谱影像文件不存在: " + params.msPath);
        return false;
    }

    // 检查文件是否可读
    QFileInfo panInfo(params.panPath);
    QFileInfo msInfo(params.msPath);

    if (!panInfo.isReadable()) {
        qDebug() << "验证失败: 全色影像文件不可读";
        QMessageBox::warning(this, "输入错误", "全色影像文件不可读");
        return false;
    }

    if (!msInfo.isReadable()) {
        qDebug() << "验证失败: 多光谱影像文件不可读";
        QMessageBox::warning(this, "输入错误", "多光谱影像文件不可读");
        return false;
    }

    // 检查文件大小
    if (panInfo.size() == 0) {
        qDebug() << "验证失败: 全色影像文件大小为0";
        QMessageBox::warning(this, "输入错误", "全色影像文件似乎是空的");
        return false;
    }

    if (msInfo.size() == 0) {
        qDebug() << "验证失败: 多光谱影像文件大小为0";
        QMessageBox::warning(this, "输入错误", "多光谱影像文件似乎是空的");
        return false;
    }

    qDebug() << "参数验证通过";
    qDebug() << "全色影像大小:" << panInfo.size() << "bytes";
    qDebug() << "多光谱影像大小:" << msInfo.size() << "bytes";

    return true;
}
void MainWindow::executeFusion(const FusionParameters& params)
{
    // 添加详细的调试信息
    qDebug() << "=== 开始影像融合 ===";
    qDebug() << "融合方法:" << params.method;
    qDebug() << "全色影像路径:" << params.panPath;
    qDebug() << "多光谱影像路径:" << params.msPath;
    qDebug() << "输出路径:" << params.outputPath;
    qDebug() << "采样比例:" << params.sampleRatio;

    // 详细验证输入文件
    QFileInfo panFile(params.panPath);
    QFileInfo msFile(params.msPath);
    QFileInfo outputFile(params.outputPath);

    qDebug() << "=== 文件检查 ===";
    qDebug() << "全色影像存在:" << panFile.exists() << "大小:" << panFile.size() << "bytes";
    qDebug() << "多光谱影像存在:" << msFile.exists() << "大小:" << msFile.size() << "bytes";
    qDebug() << "输出目录存在:" << outputFile.dir().exists();
    qDebug() << "输出目录可写:" << QFileInfo(outputFile.dir().absolutePath()).isWritable();

    if (!panFile.exists()) {
        QMessageBox::critical(this, "融合错误", "全色影像文件不存在: " + params.panPath);
        return;
    }

    if (!msFile.exists()) {
        QMessageBox::critical(this, "融合错误", "多光谱影像文件不存在: " + params.msPath);
        return;
    }

    if (!outputFile.dir().exists()) {
        QMessageBox::critical(this, "融合错误", "输出目录不存在: " + outputFile.dir().absolutePath());
        return;
    }

    if (!QFileInfo(outputFile.dir().absolutePath()).isWritable()) {
        QMessageBox::critical(this, "融合错误", "输出目录没有写权限: " + outputFile.dir().absolutePath());
        return;
    }

    // 显示进度对话框
    QProgressDialog* progressDialog = new QProgressDialog("正在进行影像融合...", "取消", 0, 100, this);
    progressDialog->setWindowTitle("融合进度");
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setMinimumDuration(0);
    progressDialog->setValue(0);
    progressDialog->show();

    // 更新进度
    auto updateProgress = [progressDialog](const QString& message, int value) {
        if (progressDialog) {
            progressDialog->setLabelText(message);
            progressDialog->setValue(value);
            QApplication::processEvents();
        }
        qDebug() << "Progress:" << message << value << "%";
        };

    updateProgress("开始影像融合...", 10);

    try {
        bool success = false;
        QString methodName = params.method;
        QString errorMessage;

        qDebug() << "=== 执行融合算法 ===";

        if (params.method == "PCA") {
            updateProgress("初始化PCA融合...", 20);
            qDebug() << "调用 porcessPCA 方法";

            try {
                // 创建进度回调函数
                auto progressCallback = [this](const QString& msg, int progress) {
                    // 将PCA内部进度映射到20%-95%范围
                    int mappedProgress = 20 + (progress * 75) / 100;
                    this->updateProgress(msg, mappedProgress);
                    QApplication::processEvents(); // 确保界面更新
                    };

                // 调用带进度回调的PCA融合
                porcessPCAWithProgress(params.msPath, params.panPath, params.outputPath, progressCallback);
                success = QFileInfo::exists(params.outputPath);
                qDebug() << "PCA融合完成，输出文件存在:" << success;
            }
            catch (const std::exception& e) {
                errorMessage = QString("PCA融合异常: %1").arg(e.what());
                qDebug() << errorMessage;
                success = false;
            }
            catch (...) {
                errorMessage = "PCA融合发生未知异常";
                qDebug() << errorMessage;
                success = false;
            }

            methodName = "PCA";
        }
        else if (params.method == "IHS") {
            updateProgress("执行IHS融合...", 20);
            qDebug() << "调用 porcessHis 方法";

            try {
                porcessHis(params.msPath, params.panPath, params.outputPath);
                success = QFileInfo::exists(params.outputPath);
                qDebug() << "IHS融合完成，输出文件存在:" << success;
            }
            catch (const std::exception& e) {
                errorMessage = QString("IHS融合异常: %1").arg(e.what());
                qDebug() << errorMessage;
                success = false;
            }
            catch (...) {
                errorMessage = "IHS融合发生未知异常";
                qDebug() << errorMessage;
                success = false;
            }

            methodName = "IHS";
        }
        else {
            errorMessage = QString("不支持的融合方法: %1").arg(params.method);
            qDebug() << errorMessage;
            success = false;
        }

        qDebug() << "=== 融合结果检查 ===";
        qDebug() << "融合成功:" << success;

        if (success) {
            QFileInfo resultFile(params.outputPath);
            qDebug() << "结果文件大小:" << resultFile.size() << "bytes";
            qDebug() << "结果文件可读:" << resultFile.isReadable();

            updateProgress("融合完成", 100);

            // 延迟一下让用户看到100%
            QTimer::singleShot(500, [this, progressDialog, params, methodName]() {
                progressDialog->close();
                delete progressDialog;

                // 显示成功消息
                QString resultMessage = QString("%1融合完成！\n\n输出文件：%2")
                    .arg(methodName)
                    .arg(params.outputPath);

                QMessageBox::StandardButton reply = QMessageBox::question(this, "融合完成",
                    resultMessage + "\n\n是否将融合结果添加到图像列表中？",
                    QMessageBox::Yes | QMessageBox::No);

                if (reply == QMessageBox::Yes) {
                    if (imageManager->importPanchromaticImage(params.outputPath)) {
                        updateImageList();
                        ui->statusbar->showMessage("融合结果已添加到图像列表", 3000);
                    }
                    else {
                        QMessageBox::warning(this, "警告", "无法将融合结果添加到图像列表");
                    }
                }

                // 询问是否打开输出目录
                QMessageBox::StandardButton openReply = QMessageBox::question(this, "融合完成",
                    "是否要打开输出目录查看结果？",
                    QMessageBox::Yes | QMessageBox::No);

                if (openReply == QMessageBox::Yes) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(params.outputPath).absolutePath()));
                }

                ui->statusbar->showMessage(QString("%1融合操作完成").arg(methodName), 5000);
                });
        }
        else {
            progressDialog->close();
            delete progressDialog;

            QString detailedError = QString("%1融合处理失败").arg(methodName);
            if (!errorMessage.isEmpty()) {
                detailedError += QString("\n\n错误详情: %1").arg(errorMessage);
            }
            detailedError += QString("\n\n请检查:\n"
                "1. 输入文件格式是否正确\n"
                "2. 输入文件是否损坏\n"
                "3. 磁盘空间是否足够\n"
                "4. 输出路径是否有写权限");

            QMessageBox::critical(this, "融合失败", detailedError);
        }
    }
    catch (const std::exception& e) {
        progressDialog->close();
        delete progressDialog;

        QString errorDetail = QString("融合过程中发生标准异常: %1").arg(e.what());
        qDebug() << errorDetail;
        QMessageBox::critical(this, "融合异常", errorDetail);
    }
    catch (...) {
        progressDialog->close();
        delete progressDialog;

        QString errorDetail = "融合过程中发生未知异常";
        qDebug() << errorDetail;
        QMessageBox::critical(this, "融合异常", errorDetail);
    }

    qDebug() << "=== 融合流程结束 ===";
}
void MainWindow::porcessPCAWithProgress(const QString& img1, const QString& img2, const QString& output, ProgressCallback callback)
{
    qDebug() << "=== PCA融合with进度回调开始 ===";

    try {
        callback("检查GDAL初始化...", 5);
        qDebug() << "检查GDAL驱动数量:" << GDALGetDriverCount();

        std::string msPath = img1.toStdString();
        std::string panPath = img2.toStdString();
        std::string outputPath = output.toStdString();

        callback("转换参数完成...", 10);

        // 记录开始时间
        QTime startTime = QTime::currentTime();

        callback("开始PCA融合处理...", 15);

        // 调用改进的PCA融合，传入进度回调
        bool result = ImprovedPCAFusion::performPCAFusionWithProgress(panPath, msPath, outputPath, 0.1,
            [callback](const std::string& msg, int progress) {
                callback(QString::fromStdString(msg), progress);
            });

        QTime endTime = QTime::currentTime();
        qDebug() << "PCA融合耗时:" << startTime.msecsTo(endTime) << "毫秒";
        qDebug() << "PCA融合返回结果:" << result;

        if (result) {
            callback("PCA融合完成！", 100);
            qDebug() << "PCA融合程序执行成功!";

            // 检查输出文件
            QFileInfo outputInfo(output);
            qDebug() << "输出文件存在:" << outputInfo.exists();
            if (outputInfo.exists()) {
                qDebug() << "输出文件大小:" << outputInfo.size() << "bytes";
            }
        }
        else {
            qDebug() << "PCA融合程序执行失败!";
            throw std::runtime_error("PCA融合执行失败");
        }
    }
    catch (const std::exception& e) {
        QString errorMsg = QString("PCA融合异常: %1").arg(e.what());
        qDebug() << errorMsg;
        std::cerr << errorMsg.toStdString() << std::endl;
        throw;
    }
    catch (...) {
        QString errorMsg = "PCA融合发生未知异常";
        qDebug() << errorMsg;
        std::cerr << errorMsg.toStdString() << std::endl;
        throw std::runtime_error("PCA融合发生未知异常");
    }

    qDebug() << "=== PCA融合with进度回调结束 ===";
}