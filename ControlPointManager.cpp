#include "ControlPointManager.h"
#include <QTableWidgetItem>
#include <QGraphicsItemGroup>
#include <QHeaderView>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <QRegularExpression>

ControlPointManager::ControlPointManager(QObject* parent)
    : QObject(parent)
{
    qDebug() << "配准控制点管理器初始化";
}

ControlPointManager::~ControlPointManager()
{
    clearAllControlPoints();
}

int ControlPointManager::addControlPoint(const QPointF& position, ControlPointType type)
{
    // 查找是否有未完成的控制点可以添加坐标
    for (auto& point : controlPoints_) {
        if (!point.hasPosition(type)) {
            // 为现有控制点添加对应类型的坐标
            point.setPosition(type, position);

            emit controlPointAdded(point.id, type);
            emit controlPointsChanged();

            qDebug() << QString("为控制点 ID:%1 添加%2坐标:(%3, %4)")
                .arg(point.id)
                .arg(type == ControlPointType::Reference ? "参考" : "目标")
                .arg(position.x()).arg(position.y());

            return point.id;
        }
    }

    // 如果没有未完成的控制点，创建新的
    // 计算新的连续ID
    int newId = getNextAvailableId();
    ControlPoint newPoint(newId);
    newPoint.setPosition(type, position);

    controlPoints_.append(newPoint);

    emit controlPointAdded(newId, type);
    emit controlPointsChanged();

    qDebug() << QString("创建新控制点 ID:%1, %2坐标:(%3, %4)")
        .arg(newId)
        .arg(type == ControlPointType::Reference ? "参考" : "目标")
        .arg(position.x()).arg(position.y());

    return newId;
}

bool ControlPointManager::removeControlPoint(int pointId)
{
    qDebug() << "准备删除控制点 ID:" << pointId;

    // 首先从数据中删除控制点
    bool found = false;
    for (int i = 0; i < controlPoints_.size(); ++i) {
        if (controlPoints_[i].id == pointId) {
            controlPoints_.removeAt(i);
            found = true;
            break;
        }
    }

    if (!found) {
        qDebug() << "控制点 ID:" << pointId << "不存在";
        return false;
    }

    // 删除图形显示项
    QPair<int, int> refKey(pointId, static_cast<int>(ControlPointType::Reference));
    QPair<int, int> targetKey(pointId, static_cast<int>(ControlPointType::Target));

    // 删除参考图像的控制点显示
    if (sceneItems_.contains(refKey)) {
        for (QGraphicsItem* item : sceneItems_[refKey]) {
            if (item && item->scene()) {
                qDebug() << "删除参考图像中的控制点图形";
                item->scene()->removeItem(item);
                delete item;
            }
        }
        sceneItems_.remove(refKey);
    }

    // 删除目标图像的控制点显示
    if (sceneItems_.contains(targetKey)) {
        for (QGraphicsItem* item : sceneItems_[targetKey]) {
            if (item && item->scene()) {
                qDebug() << "删除目标图像中的控制点图形";
                item->scene()->removeItem(item);
                delete item;
            }
        }
        sceneItems_.remove(targetKey);
    }

    qDebug() << "成功删除控制点 ID:" << pointId << "的所有显示项";

    // 只发出 controlPointRemoved 信号，不发出 controlPointsChanged
    emit controlPointRemoved(pointId);

    return true;
}

void ControlPointManager::clearAllControlPoints()
{
    if (controlPoints_.isEmpty()) {
        return;
    }

    int removedCount = controlPoints_.size();

    // 强制清除所有场景中的控制点图形项
    forceClearAllSceneItems();

    // 清除数据
    controlPoints_.clear();

    emit controlPointsChanged();
    emit allControlPointsCleared();

    qDebug() << QString("清空所有控制点，共删除 %1 个").arg(removedCount);
}

void ControlPointManager::clearAllControlPointsWithConfirmation()
{
    if (controlPoints_.isEmpty()) {
        QMessageBox::information(nullptr, "清空控制点", "当前没有控制点需要清空。");
        return;
    }

    int count = controlPoints_.size();
    QMessageBox::StandardButton reply = QMessageBox::question(nullptr,
        "清空所有控制点",
        QString("确定要删除所有 %1 个控制点吗？此操作不可撤销。").arg(count),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        clearAllControlPoints();
        QMessageBox::information(nullptr, "清空完成", QString("已成功删除 %1 个控制点。").arg(count));
    }
}

bool ControlPointManager::moveControlPoint(int pointId, const QPointF& newPosition, ControlPointType type)
{
    ControlPoint* point = findControlPoint(pointId);
    if (point && point->hasPosition(type)) {
        point->setPosition(type, newPosition);
        emit controlPointMoved(pointId, newPosition, type);
        emit controlPointsChanged();
        qDebug() << QString("移动控制点 ID:%1 %2坐标到:(%3, %4)")
            .arg(pointId)
            .arg(type == ControlPointType::Reference ? "参考" : "目标")
            .arg(newPosition.x()).arg(newPosition.y());
        return true;
    }
    return false;
}

QList<ControlPoint> ControlPointManager::getAllControlPoints() const
{
    return controlPoints_;
}

int ControlPointManager::getControlPointCount() const
{
    return controlPoints_.size();
}

int ControlPointManager::getCompleteControlPointCount() const
{
    int count = 0;
    for (const auto& point : controlPoints_) {
        if (point.isComplete()) {
            count++;
        }
    }
    return count;
}

ControlPoint* ControlPointManager::findControlPoint(int pointId)
{
    for (auto& point : controlPoints_) {
        if (point.id == pointId) {
            return &point;
        }
    }
    return nullptr;
}

int ControlPointManager::findControlPointAt(const QPointF& position, ControlPointType type, double tolerance) const
{
    for (const auto& point : controlPoints_) {
        if (point.visible && point.hasPosition(type)) {
            QPointF pointPos = point.getPosition(type);
            QPointF diff = pointPos - position;
            double distance = sqrt(diff.x() * diff.x() + diff.y() * diff.y());
            if (distance <= tolerance) {
                return point.id;
            }
        }
    }
    return -1;
}

// 新增：导出控制点到文件
bool ControlPointManager::exportControlPoints(const QString& filePath)
{
    if (controlPoints_.isEmpty()) {
        qDebug() << "没有控制点可以导出";
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "无法创建文件:" << filePath << "错误:" << file.errorString();
        return false;
    }

    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif

    // 写入文件头
    out << "# 几何配准控制点文件\n";
    out << "# 生成时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
    out << "# 格式: ID,参考X,参考Y,待配准X,待配准Y,描述,有参考点,有目标点\n";
    out << "# 坐标精度: 6位小数\n";
    out << "\n";

    // 导出每个控制点
    int exportedCount = 0;
    for (const auto& point : controlPoints_) {
        QString line = formatControlPointLine(point);
        if (!line.isEmpty()) {
            out << line << "\n";
            exportedCount++;
        }
    }

    file.close();

    qDebug() << QString("成功导出 %1 个控制点到文件: %2").arg(exportedCount).arg(filePath);

    // 发出信号
    emit controlPointsExported(exportedCount);

    return true;
}

// 新增：从文件导入控制点
bool ControlPointManager::importControlPoints(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "无法打开文件:" << filePath << "错误:" << file.errorString();
        return false;
    }

    QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    in.setEncoding(QStringConverter::Utf8);
#else
    in.setCodec("UTF-8");
#endif

    QList<ControlPoint> importedPoints;
    int lineNumber = 0;
    int validLines = 0;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        lineNumber++;

        // 跳过空行和注释行
        if (line.isEmpty() || line.startsWith("#")) {
            continue;
        }

        ControlPoint point;
        if (parseControlPointLine(line, point)) {
            // 检查ID是否已存在，如果存在则分配新ID
            bool idExists = false;
            for (const auto& existingPoint : controlPoints_) {
                if (existingPoint.id == point.id) {
                    idExists = true;
                    break;
                }
            }

            if (idExists) {
                point.id = getNextAvailableId();
                point.description = QString("CP%1").arg(point.id);
                qDebug() << QString("控制点ID冲突，重新分配ID: %1").arg(point.id);
            }

            importedPoints.append(point);
            validLines++;
        }
        else {
            qDebug() << QString("第 %1 行格式错误: %2").arg(lineNumber).arg(line);
        }
    }

    file.close();

    if (importedPoints.isEmpty()) {
        qDebug() << "没有找到有效的控制点数据";
        return false;
    }

    // 添加导入的控制点
    for (const auto& point : importedPoints) {
        controlPoints_.append(point);
    }

    qDebug() << QString("成功导入 %1 个控制点，共处理 %2 行数据").arg(importedPoints.size()).arg(lineNumber);

    // 发出信号
    emit controlPointsImported(importedPoints.size());
    emit controlPointsChanged();

    return true;
}

// 新增：格式化控制点为文本行
QString ControlPointManager::formatControlPointLine(const ControlPoint& point) const
{
    QString refX = point.hasReference ? QString::number(point.referencePosition.x(), 'f', 6) : "-";
    QString refY = point.hasReference ? QString::number(point.referencePosition.y(), 'f', 6) : "-";
    QString tarX = point.hasTarget ? QString::number(point.targetPosition.x(), 'f', 6) : "-";
    QString tarY = point.hasTarget ? QString::number(point.targetPosition.y(), 'f', 6) : "-";

    return QString("%1,%2,%3,%4,%5,%6,%7,%8")
        .arg(point.id)
        .arg(refX)
        .arg(refY)
        .arg(tarX)
        .arg(tarY)
        .arg(point.description)
        .arg(point.hasReference ? "1" : "0")
        .arg(point.hasTarget ? "1" : "0");
}

// 新增：解析文本行为控制点
bool ControlPointManager::parseControlPointLine(const QString& line, ControlPoint& point) const
{
    QStringList parts = line.split(',');
    if (parts.size() < 8) {
        return false;
    }

    bool ok;

    // 解析ID
    point.id = parts[0].trimmed().toInt(&ok);
    if (!ok || point.id <= 0) {
        return false;
    }

    // 解析参考坐标
    QString refXStr = parts[1].trimmed();
    QString refYStr = parts[2].trimmed();
    if (refXStr != "-" && refYStr != "-") {
        double refX = refXStr.toDouble(&ok);
        if (!ok) return false;
        double refY = refYStr.toDouble(&ok);
        if (!ok) return false;
        point.referencePosition = QPointF(refX, refY);
        point.hasReference = true;
    }
    else {
        point.hasReference = false;
    }

    // 解析目标坐标
    QString tarXStr = parts[3].trimmed();
    QString tarYStr = parts[4].trimmed();
    if (tarXStr != "-" && tarYStr != "-") {
        double tarX = tarXStr.toDouble(&ok);
        if (!ok) return false;
        double tarY = tarYStr.toDouble(&ok);
        if (!ok) return false;
        point.targetPosition = QPointF(tarX, tarY);
        point.hasTarget = true;
    }
    else {
        point.hasTarget = false;
    }

    // 解析描述
    point.description = parts[5].trimmed();
    if (point.description.isEmpty()) {
        point.description = QString("CP%1").arg(point.id);
    }

    // 解析标志位（可选，用于验证）
    if (parts.size() >= 7) {
        bool hasRef = (parts[6].trimmed() == "1");
        if (hasRef != point.hasReference) {
            qDebug() << "参考点标志位不匹配";
        }
    }

    if (parts.size() >= 8) {
        bool hasTar = (parts[7].trimmed() == "1");
        if (hasTar != point.hasTarget) {
            qDebug() << "目标点标志位不匹配";
        }
    }

    point.visible = true;
    return true;
}

void ControlPointManager::updateSceneDisplay(QGraphicsScene* scene, ControlPointType type)
{
    if (!scene) return;

    // 清除旧的显示项
    clearSceneDisplay(scene, type);

    // 为每个控制点创建显示项
    for (const auto& point : controlPoints_) {
        if (point.visible && point.hasPosition(type)) {
            createControlPointDisplay(scene, point, type);
        }
    }

    scene->update();
}

void ControlPointManager::clearSceneDisplay(QGraphicsScene* scene, ControlPointType type)
{
    if (!scene) return;

    // 清除指定类型的控制点图形项
    QMutableMapIterator<QPair<int, int>, QList<QGraphicsItem*>> iter(sceneItems_);
    while (iter.hasNext()) {
        iter.next();
        QPair<int, int> key = iter.key();
        if (key.second == static_cast<int>(type)) {
            for (QGraphicsItem* item : iter.value()) {
                if (item && item->scene() == scene) {
                    scene->removeItem(item);
                    delete item;
                }
            }
            iter.remove();
        }
    }
}

void ControlPointManager::createControlPointDisplay(QGraphicsScene* scene, const ControlPoint& point, ControlPointType type)
{
    if (!scene || !point.hasPosition(type)) return;

    QList<QGraphicsItem*> items;
    QPointF pos = point.getPosition(type);

    // 创建十字丝
    QGraphicsItem* crosshair = createCrosshairGraphics(pos, point.id, type);
    if (crosshair) {
        scene->addItem(crosshair);
        items.append(crosshair);
    }

    // 创建控制点标签
    QString label = QString("%1%2").arg(point.description)
        .arg(type == ControlPointType::Reference ? "R" : "T");
    QGraphicsTextItem* textItem = new QGraphicsTextItem(label);
    textItem->setPos(pos.x() + 20, pos.y() - 15);
    textItem->setDefaultTextColor(getControlPointColor(type));

    QFont font = textItem->font();
    font.setPointSize(12);
    font.setBold(true);
    textItem->setFont(font);
    textItem->setZValue(1001);

    scene->addItem(textItem);
    items.append(textItem);

    // 保存图形项
    QPair<int, int> key(point.id, static_cast<int>(type));
    sceneItems_[key] = items;
}

void ControlPointManager::removeControlPointDisplay(QGraphicsScene* scene, int pointId, ControlPointType type)
{
    if (!scene) return;

    QPair<int, int> key(pointId, static_cast<int>(type));
    if (sceneItems_.contains(key)) {
        for (QGraphicsItem* item : sceneItems_[key]) {
            if (item) {
                scene->removeItem(item);
                delete item;
            }
        }
        sceneItems_.remove(key);
    }
}

// 新增：强制清除所有场景中的控制点显示项
void ControlPointManager::forceClearAllSceneItems()
{
    qDebug() << "强制清除所有控制点图形项";

    QMutableMapIterator<QPair<int, int>, QList<QGraphicsItem*>> iter(sceneItems_);
    while (iter.hasNext()) {
        iter.next();
        QList<QGraphicsItem*> items = iter.value();
        for (QGraphicsItem* item : items) {
            if (item) {
                if (item->scene()) {
                    qDebug() << "从场景中删除控制点图形项";
                    item->scene()->removeItem(item);
                }
                delete item;
            }
        }
    }

    sceneItems_.clear();
    qDebug() << "所有控制点图形项已清除";
}

QGraphicsItem* ControlPointManager::createCrosshairGraphics(const QPointF& position, int pointId, ControlPointType type)
{
    Q_UNUSED(pointId)

        // 创建一个组合图形项包含十字丝的所有部分
        QGraphicsItemGroup* group = new QGraphicsItemGroup();

    QColor color = getControlPointColor(type);
    QPen pen(color, 2);
    QBrush brush(color);

    // 创建外圆
    QGraphicsEllipseItem* outerCircle = new QGraphicsEllipseItem(
        position.x() - 8, position.y() - 8, 16, 16);
    outerCircle->setPen(pen);
    outerCircle->setBrush(QBrush(Qt::transparent));
    outerCircle->setZValue(1000);
    group->addToGroup(outerCircle);

    // 创建内圆（实心）
    QGraphicsEllipseItem* innerCircle = new QGraphicsEllipseItem(
        position.x() - 3, position.y() - 3, 6, 6);
    innerCircle->setPen(pen);
    innerCircle->setBrush(brush);
    innerCircle->setZValue(1001);
    group->addToGroup(innerCircle);

    // 创建十字丝线条
    int crossSize = 15;

    // 水平线
    QGraphicsLineItem* hLine = new QGraphicsLineItem(
        position.x() - crossSize, position.y(),
        position.x() + crossSize, position.y());
    hLine->setPen(pen);
    hLine->setZValue(1002);
    group->addToGroup(hLine);

    // 垂直线
    QGraphicsLineItem* vLine = new QGraphicsLineItem(
        position.x(), position.y() - crossSize,
        position.x(), position.y() + crossSize);
    vLine->setPen(pen);
    vLine->setZValue(1002);
    group->addToGroup(vLine);

    return group;
}

QColor ControlPointManager::getControlPointColor(ControlPointType type) const
{
    return (type == ControlPointType::Reference) ? Qt::red : Qt::blue;
}

void ControlPointManager::updateTableWidget(QTableWidget* table)
{
    if (!table) return;

    // 设置表格结构 - 5列
    table->setColumnCount(5);
    QStringList headers;
    headers << "ID" << "参考X" << "参考Y" << "待配准X" << "待配准Y";
    table->setHorizontalHeaderLabels(headers);

    // 设置行数
    table->setRowCount(controlPoints_.size());

    // 填充数据
    for (int i = 0; i < controlPoints_.size(); ++i) {
        const ControlPoint& point = controlPoints_[i];

        // ID列
        table->setItem(i, 0, new QTableWidgetItem(QString::number(point.id)));

        // 参考图像坐标 - 提高精度到4位小数
        if (point.hasReference) {
            table->setItem(i, 1, new QTableWidgetItem(QString::number(point.referencePosition.x(), 'f', 4)));
            table->setItem(i, 2, new QTableWidgetItem(QString::number(point.referencePosition.y(), 'f', 4)));
        }
        else {
            table->setItem(i, 1, new QTableWidgetItem("-"));
            table->setItem(i, 2, new QTableWidgetItem("-"));
        }

        // 待配准图像坐标 - 提高精度到4位小数
        if (point.hasTarget) {
            table->setItem(i, 3, new QTableWidgetItem(QString::number(point.targetPosition.x(), 'f', 4)));
            table->setItem(i, 4, new QTableWidgetItem(QString::number(point.targetPosition.y(), 'f', 4)));
        }
        else {
            table->setItem(i, 3, new QTableWidgetItem("-"));
            table->setItem(i, 4, new QTableWidgetItem("-"));
        }

        // ID列设为只读
        if (table->item(i, 0)) {
            table->item(i, 0)->setFlags(table->item(i, 0)->flags() & ~Qt::ItemIsEditable);
        }

        // 设置行颜色 - 完整的控制点用绿色背景
        QColor bgColor = point.isComplete() ? QColor(220, 255, 220) : QColor(255, 240, 240);
        for (int j = 0; j < 5; ++j) {
            if (table->item(i, j)) {
                table->item(i, j)->setBackground(QBrush(bgColor));

                // 缺失坐标的单元格用灰色文字
                if ((j == 1 || j == 2) && !point.hasReference) {
                    table->item(i, j)->setForeground(QBrush(Qt::gray));
                }
                if ((j == 3 || j == 4) && !point.hasTarget) {
                    table->item(i, j)->setForeground(QBrush(Qt::gray));
                }
            }
        }
    }

    // 调整列宽 - 为更高精度的坐标预留更多空间
    table->setColumnWidth(0, 50);   // ID
    table->setColumnWidth(1, 100);  // 参考X  
    table->setColumnWidth(2, 100);  // 参考Y
    table->setColumnWidth(3, 100);  // 待配准X
    table->setColumnWidth(4, 100);  // 待配准Y

    if (table->horizontalHeader()) {
        table->horizontalHeader()->setStretchLastSection(true);
    }

    qDebug() << QString("表格更新完成，共%1个控制点，其中%2个完整")
        .arg(controlPoints_.size()).arg(getCompleteControlPointCount());
}

int ControlPointManager::getNextAvailableId() const
{
    if (controlPoints_.isEmpty()) {
        return 1;
    }

    // 找到所有现有的ID
    QSet<int> existingIds;
    for (const auto& point : controlPoints_) {
        existingIds.insert(point.id);
    }

    // 找到第一个可用的连续ID
    for (int id = 1; id <= controlPoints_.size() + 1; ++id) {
        if (!existingIds.contains(id)) {
            return id;
        }
    }

    // 如果没有找到，返回最大ID+1
    int maxId = 0;
    for (const auto& point : controlPoints_) {
        maxId = std::max(maxId, point.id);
    }
    return maxId + 1;
}