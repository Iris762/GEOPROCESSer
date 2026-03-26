#include "ProgressDialog.h"
#include <QApplication>
#include <QScreen>
#include <QDebug>

ProgressDialog::ProgressDialog(QWidget* parent)
    : QDialog(parent)
    , mainLayout(nullptr)
    , buttonLayout(nullptr)
    , messageLabel(nullptr)
    , progressBar(nullptr)
    , cancelButton(nullptr)
    , autoCloseTimer(nullptr)
    , isIndefinite_(false)
    , isCancelable_(true)
    , isFinished_(false)
    , currentValue_(0)
{
    setupUI();

    // 创建自动关闭定时器
    autoCloseTimer = new QTimer(this);
    autoCloseTimer->setSingleShot(true);
    connect(autoCloseTimer, &QTimer::timeout, this, &ProgressDialog::onAutoClose);

    // 设置对话框属性
    setModal(true);
    setFixedSize(380, 120);
    setWindowTitle("处理进度");

    // 设置窗口标志
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);

    // 居中显示
    if (parent) {
        QRect parentGeometry = parent->geometry();
        move(parentGeometry.center() - rect().center());
    }
    else {
        QScreen* screen = QApplication::primaryScreen();
        if (screen) {
            move(screen->geometry().center() - rect().center());
        }
    }
}

ProgressDialog::~ProgressDialog()
{
    // Qt会自动清理子对象
}

void ProgressDialog::setupUI()
{
    // 创建主布局
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // 消息标签
    messageLabel = new QLabel("正在处理...", this);
    messageLabel->setAlignment(Qt::AlignCenter);
    messageLabel->setWordWrap(true);
    messageLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #333;");

    // 进度条
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    progressBar->setFixedHeight(20);
    progressBar->setStyleSheet(
        "QProgressBar {"
        "    border: 1px solid #ccc;"
        "    border-radius: 5px;"
        "    background-color: #f0f0f0;"
        "    text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "    background-color: #4CAF50;"
        "    border-radius: 4px;"
        "}"
    );

    // 按钮布局
    buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    // 取消按钮
    cancelButton = new QPushButton("取消", this);
    cancelButton->setFixedSize(80, 30);
    cancelButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #f0f0f0;"
        "    border: 1px solid #ccc;"
        "    border-radius: 4px;"
        "    padding: 5px 10px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #e0e0e0;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #d0d0d0;"
        "}"
    );

    connect(cancelButton, &QPushButton::clicked, this, &ProgressDialog::onCancelClicked);

    buttonLayout->addWidget(cancelButton);
    buttonLayout->addStretch();

    // 添加到主布局
    mainLayout->addWidget(messageLabel);
    mainLayout->addWidget(progressBar);
    mainLayout->addLayout(buttonLayout);
}

void ProgressDialog::showProgress(const QString& message, int value)
{
    if (!message.isEmpty()) {
        setMessage(message);
    }

    if (value >= 0) {
        setProgress(value);
        setIndefinite(false);
    }
    else {
        setIndefinite(true);
    }

    if (!isVisible()) {
        show();
    }
}

void ProgressDialog::setProgress(int value)
{
    currentValue_ = qBound(0, value, 100);
    progressBar->setValue(currentValue_);

    if (!isIndefinite_) {
        progressBar->setFormat(QString("%1%").arg(currentValue_));
    }
}

void ProgressDialog::setMessage(const QString& message)
{
    messageLabel->setText(message);
}

void ProgressDialog::setIndefinite(bool indefinite)
{
    isIndefinite_ = indefinite;

    if (indefinite) {
        progressBar->setRange(0, 0); // 无限进度
        progressBar->setFormat("处理中...");
    }
    else {
        progressBar->setRange(0, 100);
        progressBar->setFormat(QString("%1%").arg(currentValue_));
    }
}

void ProgressDialog::finish(const QString& message, int delay)
{
    isFinished_ = true;

    if (!message.isEmpty()) {
        messageLabel->setText(message);
    }

    // 设置为完成状态
    if (!isIndefinite_) {
        progressBar->setValue(100);
        progressBar->setFormat("完成");
    }

    // 更改进度条为完成功效式
    progressBar->setStyleSheet(
        "QProgressBar {"
        "    border: 1px solid #4CAF50;"
        "    border-radius: 5px;"
        "    background-color: #f0f0f0;"
        "    text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "    background-color: #4CAF50;"
        "    border-radius: 4px;"
        "}"
    );

    // 隐藏取消按钮
    cancelButton->hide();

    // 设置自动关闭
    if (delay > 0) {
        autoCloseTimer->start(delay);
    }

    emit finished();
}

void ProgressDialog::cancel()
{
    isFinished_ = true;
    emit canceled();
    close();
}

void ProgressDialog::setCancelable(bool cancelable)
{
    isCancelable_ = cancelable;
    cancelButton->setVisible(cancelable);
}

bool ProgressDialog::isShowing() const
{
    return isVisible() && !isFinished_;
}

void ProgressDialog::onCancelClicked()
{
    if (isCancelable_) {
        cancel();
    }
}

void ProgressDialog::onAutoClose()
{
    close();
}