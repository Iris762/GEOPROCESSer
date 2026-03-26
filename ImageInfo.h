#pragma once
#ifndef IMAGEINFO_H
#define IMAGEINFO_H

#include <QString>
#include <QPixmap>

// 图像类型枚举
enum class ImageType {
    Unknown,
    Panchromatic,
    Multispectral
};

// 图像信息结构体
struct ImageInfo {
    QString filePath;    // 文件完整路径
    QString fileName;    // 文件名
    ImageType type;      // 图像类型
    int width = 0;       // 图像宽度
    int height = 0;      // 图像高度
    int channels = 0;    // 通道数
    QPixmap thumbnail;   // 缩略图
};

#endif // IMAGEINFO_H
