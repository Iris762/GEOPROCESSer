#pragma once
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QResizeEvent>
#include <QTimer>
#include <QProgressBar>
#include <QLayout>
#include <QVBoxLayout>      // 导出对话框需要
#include <QHBoxLayout>      // 导出对话框需要
#include <QMenu>
#include <QPushButton>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QGroupBox>        // 导出设置对话框需要
#include <QRadioButton>     // 导出类型选择需要
#include <QComboBox>        // 格式选择需要
#include <QLineEdit>        // 自定义输入需要
#include <QCheckBox>        // 选项设置需要
#include <QSlider>          // 质量设置需要
#include <QDialog>          // 自定义对话框需要
#include <QAbstractButton>  // 按钮处理需要
#include <QFormLayout>       // 新增
#include <QActionGroup>      // 新增
#include <QRadioButton>      // 新增
#include <QSpinBox>          // 新增
#include <QDoubleSpinBox>    // 新增
#include <QCheckBox>         // 新增
#include <QDialog>           // 新增
#include <QGroupBox>         // 新增
#include "ImageManager.h"
#include "ProjectManager.h"
#include "ImageDisplayWidget.h"
#include "GeometricRegistrationWindow.h"
#include "GeometricRegistrationWindow.h"  // 使用 GeometricRegistrationWindow
#include <QFormLayout>
#include <QActionGroup>
#include <QRadioButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    // 菜单动作槽函数
    void onImportPanchromatic();
    void onImportMultispectral();
    void onImportBatch();
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveAs();

    // 导出功能槽函数
    void onExportRegisteredImage();
    void onExportFusedImage();
    void onExportDetectionResult();
    void onExportWithSettings();
    void onBatchExportSettings();

    // 图像列表槽函数
    void onImageListItemClicked(QListWidgetItem* item);
    void onImageAdded(const ImageInfo& info);

    // 复选框相关槽函数
    void onImageListItemChanged(QListWidgetItem* item);
    void onSelectAllImages();
    void onUnselectAllImages();
    void onInvertSelection();

    // 工程管理相关槽函数
    void onProjectOpened(const QString& projectPath);
    void onProjectClosed();

    // 最近工程相关槽函数
    void onOpenRecentProject();
    void onClearRecentProjects();

    // ImageDisplayWidget 相关槽函数
    void onImageDisplayed(const QString& filePath, int width, int height, int bandCount);
    void onImageDisplayFailed(const QString& error);
    void onZoomLevelChanged(double level);
    void onImageProgressUpdate(const QString& message, int value);
    void onImageProgressFinished();
    void onImportProgress(const QString& message, int percentage);
    void onTileLoadProgress(int loaded, int total);

    // 几何配准相关槽函数（更新现有的）
    void onManualRegistration();
    void onControlPointManager();
    void onRegistrationSettings();  // 添加这一行

    // 影像融合相关槽函数
    void onImageFusionSettings();
    void onPCAFusion();
    void onIHSFusion();

    // 新增的槽函数
    void onRegistrationCompleted(const QString& resultPath);
    void onRegistrationCanceled();

private:
    void porcessHis(const QString & img1, const QString &img2, const QString &output);
    void porcessPCA(const QString& img1, const QString& img2, const QString& output);
    // 影像融合连接设置函数
    void setupImageFusionConnections();

    // 融合参数结构体
    struct FusionParameters {
        QString panPath;
        QString msPath;
        QString outputPath;
        QString method;
        double sampleRatio;
    };

    // 融合相关方法
    void showFusionDialog();
    bool validateFusionInputs(const FusionParameters& params);
    void executeFusion(const FusionParameters& params);

public:
    // 公共方法获取选中的图像
    QStringList getSelectedImagePaths() const;
    QList<ImageInfo> getSelectedImages() const;
    int getSelectedImageCount() const;

private:
    Ui::MainWindow* ui;
    ImageManager* imageManager;
    ProjectManager* projectManager;
    ImageDisplayWidget* imageDisplayWidget;
    GeometricRegistrationWindow* registrationWindow;

    // UI组件
    QProgressBar* progressBar;
    QLabel* statusLabel;
    QMenu* recentProjectsMenu;

    // 图像列表控制按钮
    QPushButton* selectAllBtn;
    QPushButton* unselectAllBtn;
    QPushButton* invertSelectionBtn;
    QLabel* selectionCountLabel;

    // 当前状态
    QString currentImagePath;

    // 初始化方法
    void setupConnections();
    void setupLayout();
    void setupImageFormats();
    void setupStatusBar();
    void connectProjectManager();
    void connectImageDisplayWidget();
    void setupRecentProjectsMenu();
    void updateRecentProjectsMenu();

    // 设置图像列表控制按钮
    void setupImageListControls();

    // 图像管理方法
    void updateImageList();

    // 更新选择状态显示
    void updateSelectionCount();

    // 进度显示方法（使用状态栏进度条）
    void showProgress(const QString& message, int value = -1, bool cancelable = false);
    void hideProgress();
    void showDialogProgress(const QString& message, int value = -1, bool cancelable = false);
    void hideDialogProgress(const QString& finishMessage = QString(), int delay = 1000);
    void updateProgress(const QString& message, int value = -1);

    // 工程数据管理方法
    void saveProjectData();
    void loadProjectData();

    // 导出相关的方法
    void exportSelectedImages(const QString& exportType, const QString& defaultPrefix);
    bool copyImageFile(const QString& sourcePath, const QString& destinationPath);
    QString generateExportFileName(const QString& originalName, const QString& prefix, const QString& suffix = "");

    // 高级导出功能
    void showExportSettingsDialog();
    void performBatchExportWithSettings(const QStringList& selectedPaths, const QString& exportDir,
        const QString& format, const QString& prefix, bool preserveMetadata);

    // 几何配准连接设置函数
    void setupGeometricRegistrationConnections();
    // 进度回调函数类型
    using ProgressCallback = std::function<void(const QString&, int)>;

    // 进度更新方法
    void updateFusionProgress(const QString& message, int percentage);
    // 进度回调函数类型
    using ProgressCallback = std::function<void(const QString&, int)>;

    // 带进度回调的PCA处理方法
    void porcessPCAWithProgress(const QString& img1, const QString& img2, const QString& output, ProgressCallback callback);
};

#endif // MAINWINDOW_H