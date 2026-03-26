#include "ProjectManager.h"
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>

ProjectManager::ProjectManager(QObject* parent)
    : QObject(parent)
{
    recentFilePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recent_projects.txt";
    QDir().mkpath(QFileInfo(recentFilePath).absolutePath());
    loadRecentProjects();
}

bool ProjectManager::newProject(const QString& filePath)
{
    // 创建一个新的工程文件，包含基本的JSON结构
    QJsonObject projectObj;
    projectObj["version"] = "1.0";
    projectObj["name"] = QFileInfo(filePath).baseName();
    projectObj["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    projectObj["images"] = QJsonArray();
    projectObj["settings"] = QJsonObject();
    projectObj["currentImagePath"] = QString(); // 添加当前图像路径字段

    QJsonDocument doc(projectObj);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "无法创建工程文件:" << filePath;
        return false;
    }

    file.write(doc.toJson());
    file.close();

    currentProjectPath = filePath;
    currentProjectData = projectObj;
    updateRecentProjects(filePath);

    qDebug() << "新工程创建成功:" << filePath;
    emit projectOpened(filePath); // 发射信号
    return true;
}

bool ProjectManager::openProject(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "无法打开工程文件:" << filePath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError) {
        qDebug() << "工程文件格式错误:" << error.errorString();
        return false;
    }

    if (!doc.isObject()) {
        qDebug() << "工程文件不是有效的JSON对象";
        return false;
    }

    // 如果有当前工程，先关闭
    if (!currentProjectPath.isEmpty()) {
        emit projectClosed();
    }

    currentProjectPath = filePath;
    currentProjectData = doc.object();
    updateRecentProjects(filePath);

    qDebug() << "工程打开成功:" << filePath;
    qDebug() << "工程名称:" << currentProjectData["name"].toString();
    qDebug() << "创建时间:" << currentProjectData["created"].toString();

    emit projectOpened(filePath); // 发射信号
    return true;
}

bool ProjectManager::saveProject()
{
    if (currentProjectPath.isEmpty()) {
        qDebug() << "没有当前工程路径";
        return false;
    }

    // 更新保存时间
    currentProjectData["modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(currentProjectData);

    QFile file(currentProjectPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "无法写入工程文件:" << currentProjectPath;
        return false;
    }

    file.write(doc.toJson());
    file.close();

    qDebug() << "工程保存成功:" << currentProjectPath;
    return true;
}

bool ProjectManager::saveAsProject(const QString& filePath)
{
    QString oldPath = currentProjectPath;
    currentProjectPath = filePath;
    currentProjectData["name"] = QFileInfo(filePath).baseName();
    bool result = saveProject();
    if (result) {
        updateRecentProjects(filePath);
        // 如果路径改变了，发射信号
        if (oldPath != filePath) {
            emit projectOpened(filePath);
        }
    }
    else {
        currentProjectPath = oldPath; // 恢复原路径
    }
    return result;
}

void ProjectManager::saveImageList(const QList<ImageInfo>& imageList)
{
    QJsonArray imageArray;

    for (const ImageInfo& info : imageList) {
        QJsonObject imageObj;
        imageObj["fileName"] = info.fileName;
        imageObj["filePath"] = info.filePath;
        imageObj["type"] = static_cast<int>(info.type);
        imageObj["width"] = info.width;
        imageObj["height"] = info.height;
        imageObj["channels"] = info.channels;
        imageObj["bandCount"] = info.bandCount;
        imageObj["importTime"] = info.importTime.toString(Qt::ISODate);

        imageArray.append(imageObj);
    }

    currentProjectData["images"] = imageArray;
    qDebug() << "图像列表已保存到工程数据，共" << imageList.size() << "个图像";
}

QList<ImageInfo> ProjectManager::loadImageList() const
{
    QList<ImageInfo> imageList;

    if (!currentProjectData.contains("images")) {
        qDebug() << "工程中没有图像数据";
        return imageList;
    }

    QJsonArray imageArray = currentProjectData["images"].toArray();

    for (const QJsonValue& value : imageArray) {
        if (!value.isObject()) continue;

        QJsonObject imageObj = value.toObject();
        ImageInfo info;

        info.fileName = imageObj["fileName"].toString();
        info.filePath = imageObj["filePath"].toString();
        info.type = static_cast<ImageType>(imageObj["type"].toInt());
        info.width = imageObj["width"].toInt();
        info.height = imageObj["height"].toInt();
        info.channels = imageObj["channels"].toInt();
        info.bandCount = imageObj["bandCount"].toInt();
        info.importTime = QDateTime::fromString(imageObj["importTime"].toString(), Qt::ISODate);

        // 不在这里检查文件是否存在，让ImageManager处理
        imageList.append(info);
    }

    qDebug() << "从工程加载了" << imageList.size() << "个图像记录";
    return imageList;
}

// *** 新增方法：设置当前图像路径 ***
void ProjectManager::setCurrentImagePath(const QString& imagePath)
{
    currentProjectData["currentImagePath"] = imagePath;
    qDebug() << "设置当前图像路径:" << imagePath;
}

// *** 新增方法：获取当前图像路径 ***
QString ProjectManager::getCurrentImagePath() const
{
    return currentProjectData["currentImagePath"].toString();
}

QString ProjectManager::getCurrentProjectPath() const
{
    return currentProjectPath;
}

QString ProjectManager::getCurrentProjectName() const
{
    if (currentProjectData.contains("name")) {
        return currentProjectData["name"].toString();
    }
    return QString();
}

QStringList ProjectManager::getRecentProjects() const
{
    return recentProjects;
}

bool ProjectManager::hasCurrentProject() const
{
    return !currentProjectPath.isEmpty();
}

// *** 新增方法：清除最近工程记录 ***
void ProjectManager::clearRecentProjects()
{
    recentProjects.clear();
    saveRecentProjects();
    qDebug() << "最近工程记录已清除";
}

// *** 新增方法：从最近工程中移除指定项目 ***
void ProjectManager::removeFromRecentProjects(const QString& filePath)
{
    if (recentProjects.removeAll(filePath) > 0) {
        saveRecentProjects();
        qDebug() << "已从最近工程中移除:" << filePath;
    }
}

// *** 新增方法：关闭当前工程 ***
void ProjectManager::closeCurrentProject()
{
    if (!currentProjectPath.isEmpty()) {
        currentProjectPath.clear();
        currentProjectData = QJsonObject();
        emit projectClosed();
        qDebug() << "当前工程已关闭";
    }
}

// *** 新增方法：获取工程信息 ***
QJsonObject ProjectManager::getProjectInfo() const
{
    QJsonObject info;
    if (hasCurrentProject()) {
        info["name"] = getCurrentProjectName();
        info["path"] = getCurrentProjectPath();
        info["created"] = currentProjectData["created"].toString();
        info["modified"] = currentProjectData["modified"].toString();

        // 统计图像数量
        QJsonArray images = currentProjectData["images"].toArray();
        info["imageCount"] = images.size();

        // 统计不同类型图像数量
        int panCount = 0, multiCount = 0, unknownCount = 0;
        for (const QJsonValue& value : images) {
            QJsonObject imageObj = value.toObject();
            int type = imageObj["type"].toInt();
            switch (type) {
            case static_cast<int>(ImageType::Panchromatic): panCount++; break;
            case static_cast<int>(ImageType::Multispectral): multiCount++; break;
            default: unknownCount++; break;
            }
        }

        QJsonObject typeCounts;
        typeCounts["panchromatic"] = panCount;
        typeCounts["multispectral"] = multiCount;
        typeCounts["unknown"] = unknownCount;
        info["imageTypes"] = typeCounts;
    }
    return info;
}

void ProjectManager::loadRecentProjects()
{
    recentProjects.clear();
    QFile file(recentFilePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty() && QFile::exists(line)) {
                recentProjects.append(line);
            }
        }
        file.close();
    }
    qDebug() << "加载了" << recentProjects.size() << "个最近工程";
}

bool ProjectManager::updateRecentProjects(const QString& filePath)
{
    recentProjects.removeAll(filePath);
    recentProjects.prepend(filePath);
    while (recentProjects.size() > 10) {  // 保持最多10个
        recentProjects.removeLast();
    }

    return saveRecentProjects();
}

// *** 新增方法：保存最近工程列表 ***
bool ProjectManager::saveRecentProjects()
{
    QFile file(recentFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "无法保存最近工程列表";
        return false;
    }

    QTextStream out(&file);
    for (const QString& path : recentProjects) {
        out << path << '\n';
    }
    file.close();

    qDebug() << "最近工程列表已更新";
    return true;
}