#pragma once
#ifndef CONTROLPOINTMANAGER_H
#define CONTROLPOINTMANAGER_H

#include <QObject>
#include <QPointF>
#include <QList>
#include <QColor>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QGraphicsItemGroup>
#include <QTableWidget>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QMap>

// 控制点类型枚举 - 必须在类外定义
enum class ControlPointType {
    Reference = 0,  // 参考图像
    Target = 1      // 待配准图像
};

// 配准控制点数据结构
struct ControlPoint {
    int id;
    QPointF referencePosition;    // 参考图像坐标
    QPointF targetPosition;       // 待配准图像坐标
    bool hasReference;            // 是否有参考点
    bool hasTarget;              // 是否有目标点
    QString description;         // 描述
    bool visible;               // 是否可见

    ControlPoint() : id(0), hasReference(false), hasTarget(false), visible(true) {}
    ControlPoint(int _id)
        : id(_id), hasReference(false), hasTarget(false), visible(true) {
        description = QString("CP%1").arg(_id);
    }

    // 检查控制点是否完整（同时有参考点和目标点）
    bool isComplete() const {
        return hasReference && hasTarget;
    }

    // 获取指定类型的坐标
    QPointF getPosition(ControlPointType type) const {
        return (type == ControlPointType::Reference) ? referencePosition : targetPosition;
    }

    // 设置指定类型的坐标
    void setPosition(ControlPointType type, const QPointF& position) {
        if (type == ControlPointType::Reference) {
            referencePosition = position;
            hasReference = true;
        }
        else {
            targetPosition = position;
            hasTarget = true;
        }
    }

    // 检查是否有指定类型的坐标
    bool hasPosition(ControlPointType type) const {
        return (type == ControlPointType::Reference) ? hasReference : hasTarget;
    }
};

class ControlPointManager : public QObject
{
    Q_OBJECT

public:
    explicit ControlPointManager(QObject* parent = nullptr);
    ~ControlPointManager();

    // 基本操作
    int addControlPoint(const QPointF& position, ControlPointType type);
    bool removeControlPoint(int pointId);
    void clearAllControlPoints();

    // 控制点移动
    bool moveControlPoint(int pointId, const QPointF& newPosition, ControlPointType type);

    // 查询
    QList<ControlPoint> getAllControlPoints() const;
    int getControlPointCount() const;
    int getCompleteControlPointCount() const;  // 获取完整控制点数量
    ControlPoint* findControlPoint(int pointId);
    int findControlPointAt(const QPointF& position, ControlPointType type, double tolerance = 15.0) const;

    // 场景显示管理
    void updateSceneDisplay(QGraphicsScene* scene, ControlPointType type);
    void clearSceneDisplay(QGraphicsScene* scene, ControlPointType type);

    // 新增：从指定场景删除特定控制点的方法
    void removeControlPointFromScene(QGraphicsScene* scene, int pointId);

    // 新增：强制清除所有场景中的控制点显示项
    void forceClearAllSceneItems();

    // 表格更新
    void updateTableWidget(QTableWidget* table);

    // 新增：控制点文件导入导出功能
    bool exportControlPoints(const QString& filePath);
    bool importControlPoints(const QString& filePath);

    // 新增：清空所有控制点（带确认）
    void clearAllControlPointsWithConfirmation();

signals:
    void controlPointAdded(int pointId, ControlPointType type);
    void controlPointRemoved(int pointId);
    void controlPointMoved(int pointId, const QPointF& newPosition, ControlPointType type);
    void controlPointsChanged();

    // 新增：导入导出相关信号
    void controlPointsImported(int count);
    void controlPointsExported(int count);
    void allControlPointsCleared();

private:
    QList<ControlPoint> controlPoints_;
    QMap<QPair<int, int>, QList<QGraphicsItem*>> sceneItems_;  // key: (pointId, type)

    // 辅助方法
    void createControlPointDisplay(QGraphicsScene* scene, const ControlPoint& point, ControlPointType type);
    void removeControlPointDisplay(QGraphicsScene* scene, int pointId, ControlPointType type);
    QGraphicsItem* createCrosshairGraphics(const QPointF& position, int pointId, ControlPointType type);
    QColor getControlPointColor(ControlPointType type) const;
    int getNextAvailableId() const;

    // 新增：私有辅助方法
    QString formatControlPointLine(const ControlPoint& point) const;
    bool parseControlPointLine(const QString& line, ControlPoint& point) const;
};

#endif // CONTROLPOINTMANAGER_H