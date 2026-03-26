#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>

class ProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProgressDialog(QWidget* parent = nullptr);
    ~ProgressDialog();

    // 显示进度
    void showProgress(const QString& message, int value = -1);

    // 设置进度值（0-100）
    void setProgress(int value);

    // 设置进度消息
    void setMessage(const QString& message);

    // 设置为无限进度模式
    void setIndefinite(bool indefinite = true);

    // 完成进度并关闭
    void finish(const QString& message = QString(), int delay = 1000);

    // 取消进度
    void cancel();

    // 设置是否可以取消
    void setCancelable(bool cancelable);

    // 是否正在显示
    bool isShowing() const;

signals:
    void canceled();
    void finished();

private slots:
    void onCancelClicked();
    void onAutoClose();

private:
    void setupUI();

    // UI组件
    QVBoxLayout* mainLayout;
    QHBoxLayout* buttonLayout;
    QLabel* messageLabel;
    QProgressBar* progressBar;
    QPushButton* cancelButton;

    // 定时器
    QTimer* autoCloseTimer;

    // 状态
    bool isIndefinite_;
    bool isCancelable_;
    bool isFinished_;
    int currentValue_;
};

#endif // PROGRESSDIALOG_H