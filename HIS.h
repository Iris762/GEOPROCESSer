#include <gdal_priv.h>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <memory>
#include <cmath>
#include "GDALManager.h"

class MemoryOptimizedFusion {
public:
    static const int TILE_SIZE = 2048;
    static const int OVERLAP = 64;

    // 重命名ImageInfo避免冲突
    struct IHSImageInfo {
        int width, height, bands;
        GDALDataType dataType;
        double geoTransform[6];
        std::string projection;

        // 添加地理范围信息
        double minX, maxX, minY, maxY;
        double pixelSizeX, pixelSizeY;
    };

    // 增强的影像信息获取函数
    static bool getImageInfo(const std::string& filePath, IHSImageInfo& info) {
        // 使用统一的GDAL管理器
        GDAL::ensureInitialized();

        GDALDataset* dataset = (GDALDataset*)GDALOpen(filePath.c_str(), GA_ReadOnly);
        if (!dataset) {
            std::cerr << "无法打开文件: " << filePath << std::endl;
            return false;
        }

        info.width = dataset->GetRasterXSize();
        info.height = dataset->GetRasterYSize();
        info.bands = dataset->GetRasterCount();
        info.dataType = dataset->GetRasterBand(1)->GetRasterDataType();

        dataset->GetGeoTransform(info.geoTransform);
        info.projection = dataset->GetProjectionRef();

        // 计算地理范围
        info.pixelSizeX = info.geoTransform[1];
        info.pixelSizeY = info.geoTransform[5]; // 通常是负值

        info.minX = info.geoTransform[0];
        info.maxX = info.minX + info.width * info.pixelSizeX;
        info.minY = info.geoTransform[3] + info.height * info.pixelSizeY;
        info.maxY = info.geoTransform[3];

        GDALClose(dataset);
        return true;
    }

    // 检查两个影像的地理配准
    static bool checkGeoAlignment(const IHSImageInfo& msInfo, const IHSImageInfo& panInfo) {
        std::cout << "\n=== 地理配准检查 ===" << std::endl;

        // 检查投影
        if (msInfo.projection != panInfo.projection) {
            std::cerr << "警告: 投影系统不同!" << std::endl;
            std::cout << "多光谱投影: " << msInfo.projection.substr(0, 50) << "..." << std::endl;
            std::cout << "全色投影: " << panInfo.projection.substr(0, 50) << "..." << std::endl;
        }

        // 显示地理范围
        std::cout << "多光谱影像范围: (" << msInfo.minX << ", " << msInfo.minY
            << ") 到 (" << msInfo.maxX << ", " << msInfo.maxY << ")" << std::endl;
        std::cout << "全色影像范围: (" << panInfo.minX << ", " << panInfo.minY
            << ") 到 (" << panInfo.maxX << ", " << panInfo.maxY << ")" << std::endl;

        // 检查重叠区域
        double overlapMinX = std::max(msInfo.minX, panInfo.minX);
        double overlapMaxX = std::min(msInfo.maxX, panInfo.maxX);
        double overlapMinY = std::max(msInfo.minY, panInfo.minY);
        double overlapMaxY = std::min(msInfo.maxY, panInfo.maxY);

        if (overlapMinX >= overlapMaxX || overlapMinY >= overlapMaxY) {
            std::cerr << "错误: 两个影像没有重叠区域!" << std::endl;
            return false;
        }

        double overlapPercent = ((overlapMaxX - overlapMinX) * (overlapMaxY - overlapMinY)) /
            ((panInfo.maxX - panInfo.minX) * (panInfo.maxY - panInfo.minY)) * 100;

        std::cout << "重叠区域百分比: " << overlapPercent << "%" << std::endl;

        if (overlapPercent < 80) {
            std::cout << "警告: 重叠区域较小，可能影响融合效果" << std::endl;
        }

        return true;
    }

    // 精确的坐标转换函数
    static void geoToPixel(const IHSImageInfo& info, double geoX, double geoY,
        double& pixelX, double& pixelY) {
        // 使用地理变换矩阵进行精确转换
        double det = info.geoTransform[1] * info.geoTransform[5] -
            info.geoTransform[2] * info.geoTransform[4];

        if (std::abs(det) < 1e-10) {
            // 简化情况：无旋转
            pixelX = (geoX - info.geoTransform[0]) / info.geoTransform[1];
            pixelY = (geoY - info.geoTransform[3]) / info.geoTransform[5];
        }
        else {
            // 完整的仿射变换逆变换
            double deltaX = geoX - info.geoTransform[0];
            double deltaY = geoY - info.geoTransform[3];

            pixelX = (info.geoTransform[5] * deltaX - info.geoTransform[2] * deltaY) / det;
            pixelY = (info.geoTransform[1] * deltaY - info.geoTransform[4] * deltaX) / det;
        }
    }

    static void pixelToGeo(const IHSImageInfo& info, double pixelX, double pixelY,
        double& geoX, double& geoY) {
        geoX = info.geoTransform[0] + pixelX * info.geoTransform[1] + pixelY * info.geoTransform[2];
        geoY = info.geoTransform[3] + pixelX * info.geoTransform[4] + pixelY * info.geoTransform[5];
    }

    // 修正的分块融合处理
    static bool performTiledFusion(const std::string& msPath,
        const std::string& panPath,
        const std::string& outputPath) {

        std::cout << "=== 修正版分块影像融合 ===" << std::endl;
        // 使用统一的GDAL管理器
        GDAL::ensureInitialized();

        // 1. 获取影像信息
        IHSImageInfo msInfo, panInfo;
        if (!getImageInfo(msPath, msInfo) || !getImageInfo(panPath, panInfo)) {
            return false;
        }

        std::cout << "多光谱影像: " << msInfo.width << "x" << msInfo.height
            << " (" << msInfo.bands << " 波段)" << std::endl;
        std::cout << "全色影像: " << panInfo.width << "x" << panInfo.height << std::endl;

        // 2. 检查地理配准
        if (!checkGeoAlignment(msInfo, panInfo)) {
            return false;
        }

        // 3. 创建输出文件（使用全色影像的参数）
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (!driver) {
            std::cerr << "无法获取GeoTIFF驱动" << std::endl;
            return false;
        }

        char** options = nullptr;
        options = CSLSetNameValue(options, "COMPRESS", "LZW");
        options = CSLSetNameValue(options, "TILED", "YES");
        options = CSLSetNameValue(options, "BLOCKXSIZE", "512");
        options = CSLSetNameValue(options, "BLOCKYSIZE", "512");

        GDALDataset* outputDataset = driver->Create(outputPath.c_str(),
            panInfo.width, panInfo.height,
            msInfo.bands, GDT_UInt16, options);
        CSLDestroy(options);

        if (!outputDataset) {
            std::cerr << "无法创建输出文件" << std::endl;
            return false;
        }

        outputDataset->SetGeoTransform(panInfo.geoTransform);
        outputDataset->SetProjection(panInfo.projection.c_str());

        // 4. 分块处理
        int numTilesX = (panInfo.width + TILE_SIZE - 1) / TILE_SIZE;
        int numTilesY = (panInfo.height + TILE_SIZE - 1) / TILE_SIZE;
        int totalTiles = numTilesX * numTilesY;

        std::cout << "\n开始处理 " << totalTiles << " 个瓦片..." << std::endl;

        for (int tileY = 0; tileY < numTilesY; tileY++) {
            for (int tileX = 0; tileX < numTilesX; tileX++) {
                int currentTile = tileY * numTilesX + tileX + 1;

                // 计算全色瓦片范围
                int panXOffset = tileX * TILE_SIZE;
                int panYOffset = tileY * TILE_SIZE;
                int panTileWidth = std::min(TILE_SIZE, panInfo.width - panXOffset);
                int panTileHeight = std::min(TILE_SIZE, panInfo.height - panYOffset);

                // 计算瓦片的地理范围
                double geoXMin, geoYMax, geoXMax, geoYMin;
                pixelToGeo(panInfo, panXOffset, panYOffset, geoXMin, geoYMax);
                pixelToGeo(panInfo, panXOffset + panTileWidth, panYOffset + panTileHeight,
                    geoXMax, geoYMin);

                // 将地理坐标转换为多光谱影像的像素坐标
                double msPixelXMin, msPixelYMin, msPixelXMax, msPixelYMax;
                geoToPixel(msInfo, geoXMin, geoYMax, msPixelXMin, msPixelYMin);
                geoToPixel(msInfo, geoXMax, geoYMin, msPixelXMax, msPixelYMax);

                // 确保坐标在有效范围内
                int msXOffset = std::max(0, static_cast<int>(std::floor(msPixelXMin)));
                int msYOffset = std::max(0, static_cast<int>(std::floor(msPixelYMin)));
                int msXEnd = std::min(msInfo.width, static_cast<int>(std::ceil(msPixelXMax)));
                int msYEnd = std::min(msInfo.height, static_cast<int>(std::ceil(msPixelYMax)));

                int msTileWidth = msXEnd - msXOffset;
                int msTileHeight = msYEnd - msYOffset;

                if (msTileWidth <= 0 || msTileHeight <= 0) {
                    std::cout << "跳过瓦片 " << currentTile << " (无重叠区域)" << std::endl;
                    continue;
                }

                if (currentTile % 50 == 0) {
                    std::cout << "处理瓦片 " << currentTile << "/" << totalTiles
                        << " - 全色(" << panXOffset << "," << panYOffset << " "
                        << panTileWidth << "x" << panTileHeight << ")"
                        << " - 多光谱(" << msXOffset << "," << msYOffset << " "
                        << msTileWidth << "x" << msTileHeight << ")" << std::endl;
                }

                // 读取影像瓦片
                std::vector<cv::Mat> panTile, msTile;
                if (!readTile(panPath, panXOffset, panYOffset, panTileWidth, panTileHeight, panTile) ||
                    !readTile(msPath, msXOffset, msYOffset, msTileWidth, msTileHeight, msTile)) {
                    std::cerr << "读取瓦片失败: " << currentTile << std::endl;
                    continue;
                }

                // 重采样多光谱瓦片到全色分辨率
                std::vector<cv::Mat> msResized;
                for (const auto& band : msTile) {
                    cv::Mat resized;
                    cv::resize(band, resized, cv::Size(panTileWidth, panTileHeight),
                        0, 0, cv::INTER_CUBIC);
                    msResized.push_back(resized);
                }

                // 执行融合
                cv::Mat fusedTile = performTileFusion(msResized, panTile[0]);

                // 写入结果
                if (!writeTile(outputDataset, fusedTile, panXOffset, panYOffset)) {
                    std::cerr << "写入瓦片失败: " << currentTile << std::endl;
                }

                // 显示进度
                if (currentTile % 100 == 0 || currentTile == totalTiles) {
                    int progress = (currentTile * 100) / totalTiles;
                    std::cout << "进度: " << progress << "% (" << currentTile
                        << "/" << totalTiles << ")" << std::endl;
                }
            }
        }

        GDALClose(outputDataset);
        std::cout << "\n融合完成! 输出文件: " << outputPath << std::endl;
        return true;
    }

    // 原有的其他函数保持不变...
    static bool readTile(const std::string& filePath,
        int xOffset, int yOffset, int tileWidth, int tileHeight,
        std::vector<cv::Mat>& bands) {
        // 确保GDAL已初始化
        GDAL::ensureInitialized();
        GDALDataset* dataset = (GDALDataset*)GDALOpen(filePath.c_str(), GA_ReadOnly);
        if (!dataset) return false;

        int numBands = dataset->GetRasterCount();
        bands.clear();
        bands.reserve(numBands);

        for (int i = 1; i <= numBands; i++) {
            GDALRasterBand* band = dataset->GetRasterBand(i);
            cv::Mat tile(tileHeight, tileWidth, CV_16UC1);

            CPLErr err = band->RasterIO(GF_Read,
                xOffset, yOffset, tileWidth, tileHeight,
                tile.data, tileWidth, tileHeight,
                GDT_UInt16, 0, 0);

            if (err != CE_None) {
                std::cerr << "读取瓦片失败" << std::endl;
                GDALClose(dataset);
                return false;
            }
            bands.push_back(tile);
        }

        GDALClose(dataset);
        return true;
    }

    static bool writeTile(GDALDataset* outputDataset,
        const cv::Mat& fusedTile,
        int xOffset, int yOffset) {
        if (fusedTile.channels() == 1) {
            GDALRasterBand* band = outputDataset->GetRasterBand(1);
            cv::Mat tile16u;
            fusedTile.convertTo(tile16u, CV_16U);

            CPLErr err = band->RasterIO(GF_Write,
                xOffset, yOffset, fusedTile.cols, fusedTile.rows,
                tile16u.data, fusedTile.cols, fusedTile.rows,
                GDT_UInt16, 0, 0);
            return err == CE_None;
        }
        else {
            std::vector<cv::Mat> channels;
            cv::split(fusedTile, channels);

            for (size_t i = 0; i < channels.size(); i++) {
                GDALRasterBand* band = outputDataset->GetRasterBand(i + 1);
                cv::Mat channel16u;
                channels[i].convertTo(channel16u, CV_16U);

                CPLErr err = band->RasterIO(GF_Write,
                    xOffset, yOffset, channels[i].cols, channels[i].rows,
                    channel16u.data, channels[i].cols, channels[i].rows,
                    GDT_UInt16, 0, 0);

                if (err != CE_None) return false;
            }
            return true;
        }
    }

private:
    static cv::Mat performTileFusion(const std::vector<cv::Mat>& msBands,
        const cv::Mat& panBand) {

        // 如果波段数少于3，使用原始强度调制方法
        if (msBands.size() < 3) {
            std::vector<cv::Mat> ms_f32;
            for (const auto& band : msBands) {
                cv::Mat band_f32;
                band.convertTo(band_f32, CV_32F);
                ms_f32.push_back(band_f32);
            }

            cv::Mat pan_f32;
            panBand.convertTo(pan_f32, CV_32F);

            cv::Mat intensity = cv::Mat::zeros(ms_f32[0].size(), CV_32F);
            for (const auto& band : ms_f32) {
                intensity += band;
            }
            intensity /= static_cast<float>(ms_f32.size());
            intensity += 1.0f;

            std::vector<cv::Mat> fused_bands;
            for (const auto& band : ms_f32) {
                cv::Mat ratio = band / intensity;
                cv::Mat fused_band = ratio.mul(pan_f32);
                fused_bands.push_back(fused_band);
            }

            cv::Mat fusedTile;
            cv::merge(fused_bands, fusedTile);
            return fusedTile;
        }

        // IHS变换融合 
        std::vector<cv::Mat> ms_f32;
        for (const auto& band : msBands) {
            cv::Mat band_f32;
            band.convertTo(band_f32, CV_32F);
            ms_f32.push_back(band_f32);
        }

        cv::Mat pan_f32;
        panBand.convertTo(pan_f32, CV_32F);

        // 步骤1: RGB到IHS变换 
        cv::Mat R = ms_f32[0];  // 红色波段
        cv::Mat G = ms_f32[1];  // 绿色波段  
        cv::Mat B = ms_f32[2];  // 蓝色波段

        // 计算强度分量 (Intensity)
        cv::Mat I = (R + G + B) / 3.0f;

        // 计算色调和饱和度的差分分量
        cv::Mat V1 = R - I;  // 红色差值
        cv::Mat V2 = G - I;  // 绿色差值
        cv::Mat V3 = B - I;  // 蓝色差值

        // 步骤2: 直方图匹配 - 将全色影像匹配到原始强度分量
        cv::Scalar i_mean, i_stddev, pan_mean, pan_stddev;
        cv::meanStdDev(I, i_mean, i_stddev);
        cv::meanStdDev(pan_f32, pan_mean, pan_stddev);

        cv::Mat pan_matched;
        if (pan_stddev[0] > 1e-6) {
            double scale = i_stddev[0] / pan_stddev[0];
            pan_matched = (pan_f32 - pan_mean[0]) * scale + i_mean[0];
        }
        else {
            pan_matched = pan_f32;
        }

        // 步骤3: IHS到RGB反变换
        cv::Mat R_fused = pan_matched + V1;
        cv::Mat G_fused = pan_matched + V2;
        cv::Mat B_fused = pan_matched + V3;

        // 步骤4: 数值范围裁剪
        cv::Mat R_clipped, G_clipped, B_clipped;
        cv::threshold(R_fused, R_clipped, 65535.0f, 65535.0f, cv::THRESH_TRUNC);
        cv::max(R_clipped, 0.0f, R_clipped);
        cv::threshold(G_fused, G_clipped, 65535.0f, 65535.0f, cv::THRESH_TRUNC);
        cv::max(G_clipped, 0.0f, G_clipped);
        cv::threshold(B_fused, B_clipped, 65535.0f, 65535.0f, cv::THRESH_TRUNC);
        cv::max(B_clipped, 0.0f, B_clipped);

        // 处理其余波段
        std::vector<cv::Mat> fused_bands;
        fused_bands.push_back(R_clipped);
        fused_bands.push_back(G_clipped);
        fused_bands.push_back(B_clipped);

        // 对于第4个波段及以后，使用简单的强度调制
        for (size_t i = 3; i < msBands.size(); i++) {
            cv::Mat ratio = ms_f32[i] / (I + 1.0f);
            cv::Mat fused_band = ratio.mul(pan_matched);

            cv::Mat band_clipped;
            cv::threshold(fused_band, band_clipped, 65535.0f, 65535.0f, cv::THRESH_TRUNC);
            cv::max(band_clipped, 0.0f, band_clipped);
            fused_bands.push_back(band_clipped);
        }

        // 合并所有波段
        cv::Mat fusedTile;
        cv::merge(fused_bands, fusedTile);
        return fusedTile;
    }
};