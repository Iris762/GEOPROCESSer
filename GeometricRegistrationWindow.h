#pragma once
#ifndef GEOMETRICREGISTRATIONWINDOW_H
#define GEOMETRICREGISTRATIONWINDOW_H

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QFrame>
#include <QtGui/QActionGroup>
#include <QtCore/QString>
#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtCore/QFileInfo>
#include <QtGui/QCloseEvent>

#include "ImageDisplayWidget.h"
#include "ControlPointManager.h"

class ImageManager;

class GeometricRegistrationWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit GeometricRegistrationWindow(ImageManager* imageManager = nullptr, QWidget* parent = nullptr);
    ~GeometricRegistrationWindow();

    void setReferenceImage(const QString& imagePath);
    void setTargetImage(const QString& imagePath);

signals:
    void registrationCompleted(const QString& outputPath);
    void windowClosed();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // 图像选择
    void onSelectReferenceImage();
    void onSelectTargetImage();
    void onReferenceImageDisplayed(const QString& filePath, int width, int height, int bandCount);
    void onTargetImageDisplayed(const QString& filePath, int width, int height, int bandCount);
    void onReferenceImageFailed(const QString& errorMessage);
    void onTargetImageFailed(const QString& errorMessage);

    // 工具栏操作
    void onAddControlPoint();
    void onDeleteControlPoint();
    void onZoomMode();
    void onPanMode();
    void onResetView();

    // 图像工具栏
    void onRefZoomIn();
    void onRefZoomOut();
    void onRefZoomFit();
    void onRefZoomActual();
    void onTargetZoomIn();
    void onTargetZoomOut();
    void onTargetZoomFit();
    void onTargetZoomActual();

    // 控制点管理
    void onControlPointAdded(int pointId, ControlPointType type);
    void onControlPointRemoved(int pointId);
    void onControlPointsChanged();
    void onControlPointTableClicked(int row, int column);

    // 新增：控制点文件操作槽函数
    void onImportControlPoints();
    void onExportControlPoints();
    void onClearAllControlPoints();
    void onControlPointsImported(int count);
    void onControlPointsExported(int count);
    void onAllControlPointsCleared();

    // 配准操作
    void onExecuteRegistration();
    void onSaveRegistration();
    void onCancelRegistration();

private:
    // UI设置
    void setupUI();
    void setupMainToolBar();
    void setupImageDisplayArea();
    void setupControlPointDock();
    void setupBottomButtons();
    void setupStatusBar();
    void setupConnections();

    // 状态更新
    void updateWindowTitle();
    void updateControlPointInfo();
    void updateStatus(const QString& message);
    void checkEnableRegistration();

    // UI组件
    QWidget* centralWidget_;
    QVBoxLayout* mainLayout_;

    // 主工具栏
    QToolBar* mainToolBar_;
    QAction* addPointAction_;
    QAction* deletePointAction_;
    QAction* zoomModeAction_;
    QAction* panModeAction_;
    QAction* resetViewAction_;

    // 标题区域
    QLabel* titleLabel_;
    QFrame* titleFrame_;

    // 图像显示区域
    QSplitter* imageSplitter_;
    QWidget* refImageContainer_;
    QWidget* targetImageContainer_;
    QVBoxLayout* refImageLayout_;
    QVBoxLayout* targetImageLayout_;
    QToolBar* refImageToolBar_;
    QToolBar* targetImageToolBar_;

    // 图像显示组件
    ImageDisplayWidget* referenceImageWidget_;
    ImageDisplayWidget* targetImageWidget_;

    // 控制点停靠窗口
    QDockWidget* controlPointDock_;
    QTableWidget* controlPointTable_;
    QWidget* controlPointWidget_;
    QVBoxLayout* controlPointLayout_;
    QPushButton* selectReferenceBtn_;
    QPushButton* selectTargetBtn_;

    // 新增：控制点文件操作按钮
    QPushButton* importControlPointsBtn_;
    QPushButton* exportControlPointsBtn_;
    QPushButton* clearAllControlPointsBtn_;

    // 底部按钮
    QWidget* bottomButtonWidget_;
    QHBoxLayout* bottomButtonLayout_;
    QPushButton* saveBtn_;
    QPushButton* cancelBtn_;
    QPushButton* executeBtn_;

    // 状态栏
    QLabel* pointCountLabel_;
    QLabel* statusLabel_;

    // 数据
    ImageManager* imageManager_;
    ControlPointManager* controlPointManager_;
    QString referenceImagePath_;
    QString targetImagePath_;
    bool referenceImageLoaded_;
    bool targetImageLoaded_;
    bool controlPointMode_;
};

#endif