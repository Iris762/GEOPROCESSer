#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QObject>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

// 引入ImageManager的头文件以使用ImageInfo
#include "ImageManager.h"

class ProjectManager : public QObject
{
    Q_OBJECT

public:
    explicit ProjectManager(QObject* parent = nullptr);

    // 工程管理
    bool newProject(const QString& filePath);
    bool openProject(const QString& filePath);
    bool saveProject();
    bool saveAsProject(const QString& filePath);

    // *** 新增：关闭当前工程 ***
    void closeCurrentProject();

    // 图像数据管理
    void saveImageList(const QList<ImageInfo>& imageList);
    QList<ImageInfo> loadImageList() const;

    // *** 新增：当前图像路径管理 ***
    void setCurrentImagePath(const QString& imagePath);
    QString getCurrentImagePath() const;

    // 工程信息查询
    QString getCurrentProjectPath() const;
    QString getCurrentProjectName() const;
    bool hasCurrentProject() const;

    // *** 新增：获取详细工程信息 ***
    QJsonObject getProjectInfo() const;

    // 最近工程管理
    QStringList getRecentProjects() const;

    // *** 新增：最近工程管理方法 ***
    void clearRecentProjects();
    void removeFromRecentProjects(const QString& filePath);

signals:
    // *** 新增：工程状态信号 ***
    void projectOpened(const QString& projectPath);
    void projectClosed();
    void projectSaved(const QString& projectPath);
    void recentProjectsChanged();

private:
    void loadRecentProjects();
    bool updateRecentProjects(const QString& filePath);

    // *** 新增：保存最近工程列表 ***
    bool saveRecentProjects();

    // 成员变量
    QString recentFilePath;
    QStringList recentProjects;
    QString currentProjectPath;
    QJsonObject currentProjectData;
};

#endif // PROJECTMANAGER_H