#include <gdal_priv.h>
#include <gdal_alg.h>
#include <gdalwarper.h>
#include <cpl_conv.h>
#include <ogr_spatialref.h>
#include <Eigen/Dense>
#include <vector>
#include <iostream>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <functional>  // 添加这个头文件支持 std::function
#include <fstream>        // ✅ 添加这个头文件解决 std::ifstream 错误
#include <stdexcept>      // ✅ 添加这个用于异常处理
#include <QRegularExpression>
#include "GDALManager.h"

// 重命名ImageInfo避免冲突
struct PCAImageInfo {
    int width, height, bands;
    GDALDataType dataType;
    double geoTransform[6];
    std::string projection;

    // 地理范围信息
    double minX, maxX, minY, maxY;
    double pixelSizeX, pixelSizeY;
};

// PCA参数结构体
struct PCAParameters {
    std::vector<double> means;                           // 各波段均值
    std::vector<std::vector<double>> eigenVectors;      // 特征向量矩阵
    std::vector<double> eigenValues;                     // 特征值
    double pc1Mean, pc1Std;                             // 第一主成分统计量
    double panMean, panStd;                             // 全色影像统计量
    int numBands;
};

// 精度评估结果结构体
struct EvaluationResults {
    double entropy;                    // 熵值
    double correlation_coefficient;    // 相关系数
    double rmse;                      // 均方根误差
    double cross_entropy;             // 交叉熵
    double psnr;                      // 峰值信噪比
};

class ImprovedPCAFusion {
public:
    // 进度回调函数类型
    using ProgressCallbackFunc = std::function<void(const std::string&, int)>;

    static const int TILE_SIZE = 1024;
    static const int OVERLAP = 64;

    /**
     * 原有的主融合函数（保持向后兼容）
     */
    static bool performPCAFusion(const std::string& panPath,
        const std::string& msPath,
        const std::string& outputPath,
        double sampleRatio = 0.1) {
        // 调用带进度回调的版本，但不传递回调函数
        return performPCAFusionWithProgress(panPath, msPath, outputPath, sampleRatio, nullptr);
    }

    /**
     * 新增：带进度回调的主融合函数
     */
    static bool performPCAFusionWithProgress(const std::string& panPath,
        const std::string& msPath,
        const std::string& outputPath,
        double sampleRatio = 0.1,
        ProgressCallbackFunc progressCallback = nullptr) {

        // 立即添加这些 - 在现有输出之前
        std::cout.flush(); // 强制刷新

        // 参数验证 - 在try块之前添加
        if (panPath.empty() || msPath.empty() || outputPath.empty()) {
            std::cerr << "错误：输入参数为空" << std::endl;
            return false;
        }

        if (sampleRatio <= 0 || sampleRatio > 1.0) {
            std::cerr << "错误：采样比例无效" << std::endl;
            return false;
        }

        // 安全的进度回调函数
        auto safeCallback = [&progressCallback](const std::string& msg, int progress) {
            if (progressCallback) {
                progressCallback(msg, progress);
            }
            std::cout << "Progress: " << msg << " " << progress << "%" << std::endl;
            };

        std::cout << "=== PCA Fusion 内部调试开始 ===" << std::endl;
        std::cout << "全色影像路径: " << panPath << std::endl;
        std::cout << "多光谱影像路径: " << msPath << std::endl;
        std::cout << "输出路径: " << outputPath << std::endl;
        std::cout << "采样比例: " << sampleRatio << std::endl;

        try {
            safeCallback("初始化GDAL...", 1);
            std::cout << "步骤1: 初始化GDAL..." << std::endl;
            // 使用统一的GDAL管理器
            GDAL::ensureInitialized();
            std::cout << "GDAL初始化完成，驱动数量: " << GDALGetDriverCount() << std::endl;

            // 1. 获取影像信息
            safeCallback("获取影像信息...", 5);
            std::cout << "步骤2: 获取影像信息..." << std::endl;
            PCAImageInfo panInfo, msInfo;

            std::cout << "正在读取全色影像信息..." << std::endl;
            if (!getImageInfo(panPath, panInfo)) {
                std::cerr << "错误: 无法获取全色影像信息" << std::endl;
                return false;
            }
            std::cout << "全色影像信息读取成功" << std::endl;

            std::cout << "正在读取多光谱影像信息..." << std::endl;
            if (!getImageInfo(msPath, msInfo)) {
                std::cerr << "错误: 无法获取多光谱影像信息" << std::endl;
                return false;
            }
            std::cout << "多光谱影像信息读取成功" << std::endl;

            safeCallback("影像信息获取完成", 10);
            std::cout << "全色影像: " << panInfo.width << "x" << panInfo.height << std::endl;
            std::cout << "多光谱影像: " << msInfo.width << "x" << msInfo.height
                << " (" << msInfo.bands << " 波段)" << std::endl;

            // 检查影像大小是否合理
            long long panPixels = (long long)panInfo.width * panInfo.height;
            long long msPixels = (long long)msInfo.width * msInfo.height * msInfo.bands;

            std::cout << "全色影像像素数: " << panPixels << std::endl;
            std::cout << "多光谱影像像素数: " << msPixels << std::endl;

            // 估算内存需求
            long long estimatedMemoryMB = (panPixels * 2 + msPixels * 2) / (1024 * 1024);
            std::cout << "预估最小内存需求: " << estimatedMemoryMB << " MB" << std::endl;

            if (estimatedMemoryMB > 4000) { // 超过4GB
                std::cout << "警告: 影像很大，可能需要大量内存" << std::endl;
            }

            // 2. 检查地理配准
            safeCallback("检查地理配准...", 15);
            std::cout << "步骤3: 检查地理配准..." << std::endl;
            if (!checkGeoAlignment(msInfo, panInfo)) {
                std::cerr << "错误: 地理配准检查失败" << std::endl;
                return false;
            }
            std::cout << "地理配准检查通过" << std::endl;

            // 3. 采样计算全局PCA参数
            safeCallback("计算PCA参数...", 20);
            std::cout << "步骤4: 开始计算全局PCA参数..." << std::endl;
            std::cout << "采样比例: " << sampleRatio << std::endl;

            PCAParameters pcaParams = computeGlobalPCAParametersWithProgress(panPath, msPath,
                panInfo, msInfo, sampleRatio, safeCallback);

            if (pcaParams.numBands == 0) {
                std::cerr << "错误: PCA参数计算失败" << std::endl;
                return false;
            }

            safeCallback("PCA参数计算完成", 35);
            std::cout << "PCA参数计算成功" << std::endl;

            // 4. 创建输出文件（使用全色影像的参数）
            safeCallback("创建输出文件...", 40);
            std::cout << "步骤5: 创建输出文件..." << std::endl;
            GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
            if (!driver) {
                std::cerr << "错误: 无法获取GeoTIFF驱动" << std::endl;
                return false;
            }
            std::cout << "GeoTIFF驱动获取成功" << std::endl;

            char** options = nullptr;
            options = CSLSetNameValue(options, "COMPRESS", "LZW");
            options = CSLSetNameValue(options, "TILED", "YES");
            options = CSLSetNameValue(options, "BLOCKXSIZE", "512");
            options = CSLSetNameValue(options, "BLOCKYSIZE", "512");
            options = CSLSetNameValue(options, "BIGTIFF", "IF_SAFER");

            std::cout << "正在创建输出数据集..." << std::endl;
            GDALDataset* outputDataset = driver->Create(outputPath.c_str(),
                panInfo.width, panInfo.height,
                msInfo.bands, GDT_UInt16, options);
            CSLDestroy(options);

            if (!outputDataset) {
                std::cerr << "错误: 无法创建输出文件" << std::endl;
                std::cerr << "GDAL错误: " << CPLGetLastErrorMsg() << std::endl;
                return false;
            }
            std::cout << "输出文件创建成功" << std::endl;

            outputDataset->SetGeoTransform(panInfo.geoTransform);
            outputDataset->SetProjection(panInfo.projection.c_str());

            // 5. 分块处理
            safeCallback("开始分块处理...", 45);
            std::cout << "步骤6: 开始分块处理..." << std::endl;
            bool success = performTiledPCAFusionWithProgress(panPath, msPath, outputDataset,
                panInfo, msInfo, pcaParams, safeCallback);

            GDALClose(outputDataset);
            std::cout << "输出数据集已关闭" << std::endl;

            if (success) {
                safeCallback("PCA融合完成！", 100);
                std::cout << "\nPCA融合完成! 输出文件: " << outputPath << std::endl;

                // 6. 进行精度评估
                std::cout << "步骤7: 开始精度评估..." << std::endl;
                evaluateFusionQuality(msPath, outputPath);

                return true;
            }
            else {
                std::cerr << "\n错误: PCA融合失败!" << std::endl;
                return false;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "PCA融合发生标准异常: " << e.what() << std::endl;
            return false;
        }
        catch (...) {
            std::cerr << "PCA融合发生未知异常" << std::endl;
            return false;
        }
    }

    /**
    * 精度评估函数
    */
    static void evaluateFusionQuality(const std::string& originalMSPath,
        const std::string& fusedPath) {
        // 确保GDAL已初始化
        GDAL::ensureInitialized();

        // 打开原始多光谱影像和融合结果
        GDALDataset* originalDS = (GDALDataset*)GDALOpen(originalMSPath.c_str(), GA_ReadOnly);
        GDALDataset* fusedDS = (GDALDataset*)GDALOpen(fusedPath.c_str(), GA_ReadOnly);

        if (!originalDS || !fusedDS) {
            if (originalDS) GDALClose(originalDS);
            if (fusedDS) GDALClose(fusedDS);
            return;
        }

        // 获取影像信息
        int origWidth = originalDS->GetRasterXSize();
        int origHeight = originalDS->GetRasterYSize();
        int fusedWidth = fusedDS->GetRasterXSize();
        int fusedHeight = fusedDS->GetRasterYSize();
        int numBands = originalDS->GetRasterCount();

        // 计算采样参数
        int targetSamples = 50000;
        int sampleStep = std::max(1, (int)std::sqrt(origWidth * origHeight / targetSamples));

        EvaluationResults results = calculateEvaluationMetrics(originalDS, fusedDS, sampleStep);

        // 输出评估结果
        printEvaluationResults(results);

        GDALClose(originalDS);
        GDALClose(fusedDS);
    }

private:
    /**
   * 带进度回调的PCA参数计算
   */
    static PCAParameters computeGlobalPCAParametersWithProgress(const std::string& panPath,
        const std::string& msPath,
        const PCAImageInfo& panInfo,
        const PCAImageInfo& msInfo,
        double sampleRatio,
        ProgressCallbackFunc progressCallback) {

        // 安全的进度回调
        auto safeCallback = [&progressCallback](const std::string& msg, int progress) {
            if (progressCallback) {
                progressCallback(msg, progress);
            }
            };

        // 确保GDAL已初始化
        GDAL::ensureInitialized();

        PCAParameters params;
        params.numBands = msInfo.bands;
        params.means.resize(msInfo.bands, 0.0);
        params.eigenVectors.resize(msInfo.bands, std::vector<double>(msInfo.bands, 0.0));
        params.eigenValues.resize(msInfo.bands, 0.0);

        safeCallback("打开影像文件...", 21);

        GDALDataset* panDS = (GDALDataset*)GDALOpen(panPath.c_str(), GA_ReadOnly);
        GDALDataset* msDS = (GDALDataset*)GDALOpen(msPath.c_str(), GA_ReadOnly);

        if (!panDS || !msDS) {
            std::cerr << "无法打开影像文件进行采样" << std::endl;
            if (panDS) GDALClose(panDS);
            if (msDS) GDALClose(msDS);
            params.numBands = 0;
            return params;
        }

        // 计算采样点数
        int totalPixels = panInfo.width * panInfo.height;
        int targetSampleCount = static_cast<int>(totalPixels * sampleRatio);
        targetSampleCount = std::min(targetSampleCount, 50000);

        std::cout << "目标采样点数: " << targetSampleCount << std::endl;

        safeCallback("开始采样数据...", 25);

        // 地理配准采样
        std::vector<std::vector<double>> sampleData = sampleWithGeoAlignment(panDS, msDS,
            panInfo, msInfo, targetSampleCount);

        GDALClose(panDS);
        GDALClose(msDS);

        int actualSampleCount = static_cast<int>(sampleData.size());
        std::cout << "实际采样点数: " << actualSampleCount << std::endl;

        if (actualSampleCount < 10) {
            std::cerr << "采样点数太少，无法计算PCA参数" << std::endl;
            params.numBands = 0;
            return params;
        }

        safeCallback("计算统计量...", 30);

        // 计算多光谱数据的均值
        for (int b = 0; b < msInfo.bands; b++) {
            double sum = 0.0;
            for (int s = 0; s < actualSampleCount; s++) {
                sum += sampleData[s][b + 1]; // +1因为第0个是全色数据
            }
            params.means[b] = sum / actualSampleCount;
        }

        safeCallback("计算协方差矩阵...", 32);

        // 计算协方差矩阵
        std::vector<std::vector<double>> covMatrix(msInfo.bands, std::vector<double>(msInfo.bands, 0.0));
        for (int i = 0; i < msInfo.bands; i++) {
            for (int j = 0; j < msInfo.bands; j++) {
                double covariance = 0.0;
                for (int s = 0; s < actualSampleCount; s++) {
                    double dev_i = sampleData[s][i + 1] - params.means[i];
                    double dev_j = sampleData[s][j + 1] - params.means[j];
                    covariance += dev_i * dev_j;
                }
                covMatrix[i][j] = covariance / (actualSampleCount - 1);
            }
        }

        safeCallback("特征值分解...", 34);

        // 计算特征向量和特征值
        params.eigenVectors = computeEigenVectors(covMatrix, msInfo.bands);

        // ... 继续现有的PCA参数计算代码 ...

        return params;
    }

    /**
     * 带进度回调的分块处理
     */
    static bool performTiledPCAFusionWithProgress(const std::string& panPath,
        const std::string& msPath,
        GDALDataset* outputDataset,
        const PCAImageInfo& panInfo,
        const PCAImageInfo& msInfo,
        const PCAParameters& pcaParams,
        ProgressCallbackFunc progressCallback) {

        // 安全的进度回调
        auto safeCallback = [&progressCallback](const std::string& msg, int progress) {
            if (progressCallback) {
                progressCallback(msg, progress);
            }
            };

        // 确保GDAL已初始化
        GDAL::ensureInitialized();

        GDALDataset* panDS = (GDALDataset*)GDALOpen(panPath.c_str(), GA_ReadOnly);
        GDALDataset* msDS = (GDALDataset*)GDALOpen(msPath.c_str(), GA_ReadOnly);

        if (!panDS || !msDS) {
            std::cerr << "无法打开影像文件进行融合" << std::endl;
            if (panDS) GDALClose(panDS);
            if (msDS) GDALClose(msDS);
            return false;
        }

        // 分块处理
        int numTilesX = (panInfo.width + TILE_SIZE - 1) / TILE_SIZE;
        int numTilesY = (panInfo.height + TILE_SIZE - 1) / TILE_SIZE;
        int totalTiles = numTilesX * numTilesY;

        std::cout << "\n开始处理 " << totalTiles << " 个瓦片..." << std::endl;

        bool success = true;
        int processedTiles = 0;

        for (int tileY = 0; tileY < numTilesY && success; tileY++) {
            for (int tileX = 0; tileX < numTilesX && success; tileX++) {
                processedTiles++;

                // 计算全色瓦片范围
                int panXOffset = tileX * TILE_SIZE;
                int panYOffset = tileY * TILE_SIZE;
                int panTileWidth = std::min(TILE_SIZE, panInfo.width - panXOffset);
                int panTileHeight = std::min(TILE_SIZE, panInfo.height - panYOffset);

                // 处理当前瓦片
                success = processPCATile(panDS, msDS, outputDataset,
                    panXOffset, panYOffset, panTileWidth, panTileHeight,
                    panInfo, msInfo, pcaParams);

                // 计算进度（从45%到95%）
                int progress = 45 + (processedTiles * 50) / totalTiles;
                std::string progressMsg = "处理瓦片 " + std::to_string(processedTiles) + "/" + std::to_string(totalTiles);

                safeCallback(progressMsg, progress);

                // 每处理10个瓦片或到达末尾时显示进度
                if (processedTiles % 10 == 0 || processedTiles == totalTiles) {
                    std::cout << "进度: " << progress << "% (" << processedTiles
                        << "/" << totalTiles << ")" << std::endl;
                }
            }
        }

        GDALClose(panDS);
        GDALClose(msDS);

        return success;
    }


    /**
     * 计算评估指标
     */
    static EvaluationResults calculateEvaluationMetrics(GDALDataset* originalDS,
        GDALDataset* fusedDS,
        int sampleStep) {

        EvaluationResults results = { 0 };

        int origWidth = originalDS->GetRasterXSize();
        int origHeight = originalDS->GetRasterYSize();
        int fusedWidth = fusedDS->GetRasterXSize();
        int fusedHeight = fusedDS->GetRasterYSize();
        int numBands = originalDS->GetRasterCount();

        std::vector<std::vector<double>> originalSamples;
        std::vector<std::vector<double>> fusedSamples;

        // 采样收集数据
        for (int y = 0; y < origHeight; y += sampleStep) {
            for (int x = 0; x < origWidth; x += sampleStep) {
                double scaleX = (double)fusedWidth / origWidth;
                double scaleY = (double)fusedHeight / origHeight;

                double fusedX = (x + 0.5) * scaleX - 0.5;
                double fusedY = (y + 0.5) * scaleY - 0.5;

                int fx = (int)std::round(fusedX);
                int fy = (int)std::round(fusedY);

                if (fx >= 0 && fx < fusedWidth && fy >= 0 && fy < fusedHeight) {
                    std::vector<double> origPixel(numBands);
                    std::vector<double> fusedPixel(numBands);

                    bool validSample = true;

                    // 读取原始像素
                    for (int b = 0; b < numBands; b++) {
                        double value;
                        CPLErr err = originalDS->GetRasterBand(b + 1)->RasterIO(
                            GF_Read, x, y, 1, 1, &value, 1, 1, GDT_Float64, 0, 0);
                        if (err != CE_None) {
                            validSample = false;
                            break;
                        }
                        origPixel[b] = value;
                    }

                    // 读取融合像素
                    if (validSample) {
                        for (int b = 0; b < numBands; b++) {
                            double value;
                            CPLErr err = fusedDS->GetRasterBand(b + 1)->RasterIO(
                                GF_Read, fx, fy, 1, 1, &value, 1, 1, GDT_Float64, 0, 0);
                            if (err != CE_None) {
                                validSample = false;
                                break;
                            }
                            fusedPixel[b] = value;
                        }
                    }

                    if (validSample) {
                        originalSamples.push_back(origPixel);
                        fusedSamples.push_back(fusedPixel);
                    }
                }
            }
        }

        if (originalSamples.size() < 100) {
            return results;
        }

        // 计算各项指标
        results.entropy = calculateEntropy(fusedSamples);
        results.correlation_coefficient = calculateCorrelation(originalSamples, fusedSamples);
        results.rmse = calculateRMSE(originalSamples, fusedSamples);
        results.cross_entropy = calculateCrossEntropy(originalSamples, fusedSamples);
        results.psnr = calculatePSNR(originalSamples, fusedSamples);

        return results;
    }

    static bool getImageInfo(const std::string& filePath, PCAImageInfo& info) {
        std::cout << "正在打开文件: " << filePath << std::endl;

        // 添加这个文件存在检查
        std::ifstream file(filePath, std::ios::binary);
        if (!file.good()) {
            std::cerr << "错误: 文件不存在或无法访问: " << filePath << std::endl;
            return false;
        }
        file.close();
        std::cout << "文件访问验证通过" << std::endl;

        // 重置GDAL错误
        CPLErrorReset();

        // 确保GDAL已初始化
        GDAL::ensureInitialized();
        GDALDataset* dataset = (GDALDataset*)GDALOpen(filePath.c_str(), GA_ReadOnly);
        if (!dataset) {
            std::cerr << "错误: 无法打开文件: " << filePath << std::endl;
            // 获取GDAL错误信息
            std::cerr << "GDAL错误: " << CPLGetLastErrorMsg() << std::endl;
            return false;
        }
        std::cout << "文件打开成功" << std::endl;

        info.width = dataset->GetRasterXSize();
        info.height = dataset->GetRasterYSize();
        info.bands = dataset->GetRasterCount();

        std::cout << "影像尺寸: " << info.width << " x " << info.height << std::endl;
        std::cout << "波段数: " << info.bands << std::endl;

        if (info.width <= 0 || info.height <= 0 || info.bands <= 0) {
            std::cerr << "错误: 影像尺寸或波段数无效" << std::endl;
            GDALClose(dataset);
            return false;
        }

        info.dataType = dataset->GetRasterBand(1)->GetRasterDataType();
        std::cout << "数据类型: " << GDALGetDataTypeName(info.dataType) << std::endl;

        CPLErr geoErr = dataset->GetGeoTransform(info.geoTransform);
        if (geoErr != CE_None) {
            std::cout << "警告: 无法获取地理变换信息" << std::endl;
        }
        else {
            std::cout << "地理变换信息获取成功" << std::endl;
        }

        info.projection = dataset->GetProjectionRef();
        std::cout << "投影信息长度: " << info.projection.length() << " 字符" << std::endl;

        // 计算地理范围
        info.pixelSizeX = info.geoTransform[1];
        info.pixelSizeY = info.geoTransform[5]; // 通常是负值

        info.minX = info.geoTransform[0];
        info.maxX = info.minX + info.width * info.pixelSizeX;
        info.minY = info.geoTransform[3] + info.height * info.pixelSizeY;
        info.maxY = info.geoTransform[3];

        std::cout << "地理范围: (" << info.minX << ", " << info.minY << ") 到 ("
            << info.maxX << ", " << info.maxY << ")" << std::endl;

        GDALClose(dataset);
        std::cout << "文件信息读取完成并关闭" << std::endl;
        return true;
    }
    // 检查地理配准 - 使用新的结构体名称
    static bool checkGeoAlignment(const PCAImageInfo& msInfo, const PCAImageInfo& panInfo) {
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

    // 坐标转换函数 - 使用新的结构体名称
    static void geoToPixel(const PCAImageInfo& info, double geoX, double geoY,
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

    static void pixelToGeo(const PCAImageInfo& info, double pixelX, double pixelY,
        double& geoX, double& geoY) {
        geoX = info.geoTransform[0] + pixelX * info.geoTransform[1] + pixelY * info.geoTransform[2];
        geoY = info.geoTransform[3] + pixelX * info.geoTransform[4] + pixelY * info.geoTransform[5];
    }

    /**
     * 采样计算全局PCA参数（改进版）- 使用新的结构体名称
     */
    static PCAParameters computeGlobalPCAParameters(const std::string& panPath,
        const std::string& msPath,
        const PCAImageInfo& panInfo,
        const PCAImageInfo& msInfo,
        double sampleRatio) {
        // 确保GDAL已初始化
        GDAL::ensureInitialized();

        PCAParameters params;
        params.numBands = msInfo.bands;
        params.means.resize(msInfo.bands, 0.0);
        params.eigenVectors.resize(msInfo.bands, std::vector<double>(msInfo.bands, 0.0));
        params.eigenValues.resize(msInfo.bands, 0.0);

        GDALDataset* panDS = (GDALDataset*)GDALOpen(panPath.c_str(), GA_ReadOnly);
        GDALDataset* msDS = (GDALDataset*)GDALOpen(msPath.c_str(), GA_ReadOnly);

        if (!panDS || !msDS) {
            std::cerr << "无法打开影像文件进行采样" << std::endl;
            if (panDS) GDALClose(panDS);
            if (msDS) GDALClose(msDS);
            params.numBands = 0;
            return params;
        }

        // 计算采样点数
        int totalPixels = panInfo.width * panInfo.height;
        int targetSampleCount = static_cast<int>(totalPixels * sampleRatio);
        targetSampleCount = std::min(targetSampleCount, 50000);

        std::cout << "目标采样点数: " << targetSampleCount << std::endl;

        // 地理配准采样
        std::vector<std::vector<double>> sampleData = sampleWithGeoAlignment(panDS, msDS,
            panInfo, msInfo,
            targetSampleCount);

        GDALClose(panDS);
        GDALClose(msDS);

        int actualSampleCount = static_cast<int>(sampleData.size());
        std::cout << "实际采样点数: " << actualSampleCount << std::endl;

        if (actualSampleCount < 10) {
            std::cerr << "采样点数太少，无法计算PCA参数" << std::endl;
            params.numBands = 0;
            return params;
        }

        // 计算多光谱数据的均值
        for (int b = 0; b < msInfo.bands; b++) {
            double sum = 0.0;
            for (int s = 0; s < actualSampleCount; s++) {
                sum += sampleData[s][b + 1]; // +1因为第0个是全色数据
            }
            params.means[b] = sum / actualSampleCount;
        }

        // 计算协方差矩阵
        std::vector<std::vector<double>> covMatrix(msInfo.bands, std::vector<double>(msInfo.bands, 0.0));
        for (int i = 0; i < msInfo.bands; i++) {
            for (int j = 0; j < msInfo.bands; j++) {
                double covariance = 0.0;
                for (int s = 0; s < actualSampleCount; s++) {
                    double dev_i = sampleData[s][i + 1] - params.means[i];
                    double dev_j = sampleData[s][j + 1] - params.means[j];
                    covariance += dev_i * dev_j;
                }
                covMatrix[i][j] = covariance / (actualSampleCount - 1);
            }
        }

        // 计算特征向量和特征值 - 修复参数调用
        params.eigenVectors = computeEigenVectors(covMatrix, msInfo.bands);

        // 计算第一主成分
        std::vector<double> pc1Values(actualSampleCount);
        for (int s = 0; s < actualSampleCount; s++) {
            double pc1 = 0.0;
            for (int b = 0; b < msInfo.bands; b++) {
                pc1 += params.eigenVectors[0][b] * (sampleData[s][b + 1] - params.means[b]);
            }
            pc1Values[s] = pc1;
        }

        // 计算第一主成分统计量
        double pc1Sum = 0.0;
        for (int s = 0; s < actualSampleCount; s++) {
            pc1Sum += pc1Values[s];
        }
        params.pc1Mean = pc1Sum / actualSampleCount;

        double pc1Variance = 0.0;
        for (int s = 0; s < actualSampleCount; s++) {
            double dev = pc1Values[s] - params.pc1Mean;
            pc1Variance += dev * dev;
        }
        params.pc1Std = std::sqrt(pc1Variance / (actualSampleCount - 1));

        // 计算全色影像统计量
        double panSum = 0.0;
        for (int s = 0; s < actualSampleCount; s++) {
            panSum += sampleData[s][0]; // 全色数据在索引0
        }
        params.panMean = panSum / actualSampleCount;

        double panVariance = 0.0;
        for (int s = 0; s < actualSampleCount; s++) {
            double dev = sampleData[s][0] - params.panMean;
            panVariance += dev * dev;
        }
        params.panStd = std::sqrt(panVariance / (actualSampleCount - 1));

        // 避免除零错误
        if (params.panStd < 1e-10) params.panStd = 1.0;
        if (params.pc1Std < 1e-10) params.pc1Std = 1.0;

        std::cout << "全色影像统计 - 均值: " << params.panMean << ", 标准差: " << params.panStd << std::endl;
        std::cout << "第一主成分统计 - 均值: " << params.pc1Mean << ", 标准差: " << params.pc1Std << std::endl;

        return params;
    }

    // 地理配准采样 - 使用新的结构体名称
    static std::vector<std::vector<double>> sampleWithGeoAlignment(GDALDataset* panDS,
        GDALDataset* msDS,
        const PCAImageInfo& panInfo,
        const PCAImageInfo& msInfo,
        int targetSampleCount) {

        std::vector<std::vector<double>> samples;
        samples.reserve(targetSampleCount);

        // 计算采样步长
        int step = static_cast<int>(std::sqrt(panInfo.width * panInfo.height / targetSampleCount));
        step = std::max(step, 1);

        for (int panY = 0; panY < panInfo.height; panY += step) {
            for (int panX = 0; panX < panInfo.width; panX += step) {
                if (static_cast<int>(samples.size()) >= targetSampleCount) {
                    break;
                }

                // 将全色像素坐标转换为地理坐标
                double geoX, geoY;
                pixelToGeo(panInfo, panX + 0.5, panY + 0.5, geoX, geoY);

                // 将地理坐标转换为多光谱像素坐标
                double msPixelX, msPixelY;
                geoToPixel(msInfo, geoX, geoY, msPixelX, msPixelY);

                // 检查是否在多光谱影像范围内
                int msX = static_cast<int>(std::round(msPixelX));
                int msY = static_cast<int>(std::round(msPixelY));

                if (msX < 0 || msX >= msInfo.width || msY < 0 || msY >= msInfo.height) {
                    continue;
                }

                std::vector<double> pixel(msInfo.bands + 1);

                // 读取全色像素
                double panPixel = 0.0;
                CPLErr panErr = panDS->GetRasterBand(1)->RasterIO(GF_Read, panX, panY, 1, 1, &panPixel,
                    1, 1, GDT_Float64, 0, 0);
                if (panErr != CE_None) continue;
                pixel[0] = panPixel;

                // 读取多光谱像素
                bool validPixel = true;
                for (int b = 0; b < msInfo.bands; b++) {
                    double msPixel = 0.0;
                    CPLErr msErr = msDS->GetRasterBand(b + 1)->RasterIO(GF_Read, msX, msY, 1, 1, &msPixel,
                        1, 1, GDT_Float64, 0, 0);
                    if (msErr != CE_None) {
                        validPixel = false;
                        break;
                    }
                    pixel[b + 1] = msPixel;
                }

                if (validPixel) {
                    samples.push_back(pixel);
                }
            }
            if (static_cast<int>(samples.size()) >= targetSampleCount) {
                break;
            }
        }

        return samples;
    }

    /**
     * 计算特征向量和特征值 - 修复参数
     */
    static std::vector<std::vector<double>> computeEigenVectors(
        const std::vector<std::vector<double>>& covMatrix, int numBands) {

        std::cout << "开始特征值分解..." << std::endl;

        // 转换为Eigen矩阵
        Eigen::MatrixXd eigenMatrix(numBands, numBands);
        for (int i = 0; i < numBands; i++) {
            for (int j = 0; j < numBands; j++) {
                eigenMatrix(i, j) = covMatrix[i][j];
            }
        }

        // 计算特征值和特征向量
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(eigenMatrix);

        if (solver.info() != Eigen::Success) {
            std::cerr << "特征值分解失败！使用单位矩阵" << std::endl;
            std::vector<std::vector<double>> identity(numBands, std::vector<double>(numBands, 0.0));
            for (int i = 0; i < numBands; i++) {
                identity[i][i] = 1.0;
            }
            return identity;
        }

        Eigen::MatrixXd eigenvectors = solver.eigenvectors();
        Eigen::VectorXd eigenvalues = solver.eigenvalues();

        // 按特征值从大到小排序
        std::vector<std::pair<double, int>> eigenPairs;
        for (int i = 0; i < numBands; i++) {
            eigenPairs.push_back(std::make_pair(eigenvalues(i), i));
        }
        std::sort(eigenPairs.begin(), eigenPairs.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        // 转换回标准格式
        std::vector<std::vector<double>> result(numBands, std::vector<double>(numBands));
        for (int i = 0; i < numBands; i++) {
            int originalIndex = eigenPairs[i].second;
            for (int j = 0; j < numBands; j++) {
                result[i][j] = eigenvectors(j, originalIndex);
            }
        }

        // 输出特征值
        std::cout << "特征值 (降序): ";
        for (int i = 0; i < numBands; i++) {
            std::cout << eigenPairs[i].first << " ";
        }
        std::cout << std::endl;

        return result;
    }
    // 分块PCA融合处理 - 使用新的结构体名称
    static bool performTiledPCAFusion(const std::string& panPath,
        const std::string& msPath,
        GDALDataset* outputDataset,
        const PCAImageInfo& panInfo,
        const PCAImageInfo& msInfo,
        const PCAParameters& pcaParams) {
        // 确保GDAL已初始化
        GDAL::ensureInitialized();

        GDALDataset* panDS = (GDALDataset*)GDALOpen(panPath.c_str(), GA_ReadOnly);
        GDALDataset* msDS = (GDALDataset*)GDALOpen(msPath.c_str(), GA_ReadOnly);

        if (!panDS || !msDS) {
            std::cerr << "无法打开影像文件进行融合" << std::endl;
            if (panDS) GDALClose(panDS);
            if (msDS) GDALClose(msDS);
            return false;
        }

        // 分块处理
        int numTilesX = (panInfo.width + TILE_SIZE - 1) / TILE_SIZE;
        int numTilesY = (panInfo.height + TILE_SIZE - 1) / TILE_SIZE;
        int totalTiles = numTilesX * numTilesY;

        std::cout << "\n开始处理 " << totalTiles << " 个瓦片..." << std::endl;

        bool success = true;

        for (int tileY = 0; tileY < numTilesY && success; tileY++) {
            for (int tileX = 0; tileX < numTilesX && success; tileX++) {
                int currentTile = tileY * numTilesX + tileX + 1;

                // 计算全色瓦片范围
                int panXOffset = tileX * TILE_SIZE;
                int panYOffset = tileY * TILE_SIZE;
                int panTileWidth = std::min(TILE_SIZE, panInfo.width - panXOffset);
                int panTileHeight = std::min(TILE_SIZE, panInfo.height - panYOffset);

                // 处理当前瓦片
                success = processPCATile(panDS, msDS, outputDataset,
                    panXOffset, panYOffset, panTileWidth, panTileHeight,
                    panInfo, msInfo, pcaParams);

                // 显示进度
                if (currentTile % 50 == 0 || currentTile == totalTiles) {
                    int progress = (currentTile * 100) / totalTiles;
                    std::cout << "进度: " << progress << "% (" << currentTile
                        << "/" << totalTiles << ")" << std::endl;
                }
            }
        }

        GDALClose(panDS);
        GDALClose(msDS);

        return success;
    }

    /**
     * 处理单个PCA瓦片（改进版）- 使用新的结构体名称
     */
    static bool processPCATile(GDALDataset* panDS, GDALDataset* msDS, GDALDataset* outputDS,
        int panXOffset, int panYOffset, int panTileWidth, int panTileHeight,
        const PCAImageInfo& panInfo, const PCAImageInfo& msInfo,
        const PCAParameters& pcaParams) {

        // 读取全色瓦片
        std::vector<double> panData(panTileWidth * panTileHeight);
        CPLErr panErr = panDS->GetRasterBand(1)->RasterIO(GF_Read, panXOffset, panYOffset,
            panTileWidth, panTileHeight,
            panData.data(), panTileWidth, panTileHeight,
            GDT_Float64, 0, 0);
        if (panErr != CE_None) {
            std::cerr << "读取全色瓦片失败" << std::endl;
            return false;
        }

        // 创建重采样的多光谱瓦片
        std::vector<std::vector<double>> msResampled(msInfo.bands,
            std::vector<double>(panTileWidth * panTileHeight));

        // 使用地理配准进行精确重采样
        for (int py = 0; py < panTileHeight; py++) {
            for (int px = 0; px < panTileWidth; px++) {
                // 全色像素的绝对坐标
                int globalPanX = panXOffset + px;
                int globalPanY = panYOffset + py;

                // 转换为地理坐标
                double geoX, geoY;
                pixelToGeo(panInfo, globalPanX + 0.5, globalPanY + 0.5, geoX, geoY);

                // 转换为多光谱像素坐标
                double msPixelX, msPixelY;
                geoToPixel(msInfo, geoX, geoY, msPixelX, msPixelY);

                int pixelIndex = py * panTileWidth + px;

                // 双线性插值采样多光谱数据
                for (int b = 0; b < msInfo.bands; b++) {
                    double value = bilinearInterpolate(msDS->GetRasterBand(b + 1),
                        msPixelX, msPixelY, msInfo);
                    msResampled[b][pixelIndex] = value;
                }
            }
        }

        // 应用PCA融合
        std::vector<std::vector<double>> fusedData = applyPCAFusion(panData, msResampled,
            panTileWidth * panTileHeight,
            pcaParams);

        // 写入融合结果
        for (int b = 0; b < msInfo.bands; b++) {
            std::vector<uint16_t> bandData(panTileWidth * panTileHeight);
            for (int i = 0; i < panTileWidth * panTileHeight; i++) {
                double val = fusedData[b][i];
                if (val < 0.0) val = 0.0;
                if (val > 65535.0) val = 65535.0;
                bandData[i] = static_cast<uint16_t>(val + 0.5);
            }

            CPLErr writeErr = outputDS->GetRasterBand(b + 1)->RasterIO(
                GF_Write, panXOffset, panYOffset, panTileWidth, panTileHeight,
                bandData.data(), panTileWidth, panTileHeight, GDT_UInt16, 0, 0);

            if (writeErr != CE_None) {
                std::cerr << "写入波段数据失败" << std::endl;
                return false;
            }
        }

        return true;
    }

    /**
     * 双线性插值（新增）- 使用新的结构体名称
     */
    static double bilinearInterpolate(GDALRasterBand* band, double x, double y, const PCAImageInfo& info) {
        int x0 = static_cast<int>(std::floor(x));
        int y0 = static_cast<int>(std::floor(y));
        int x1 = x0 + 1;
        int y1 = y0 + 1;

        // 边界检查
        if (x0 < 0 || y0 < 0 || x1 >= info.width || y1 >= info.height) {
            // 如果在边界附近，使用最近邻
            int nearX = static_cast<int>(std::round(x));
            int nearY = static_cast<int>(std::round(y));
            nearX = std::max(0, std::min(nearX, info.width - 1));
            nearY = std::max(0, std::min(nearY, info.height - 1));

            double value = 0.0;
            band->RasterIO(GF_Read, nearX, nearY, 1, 1, &value, 1, 1, GDT_Float64, 0, 0);
            return value;
        }

        // 读取四个角点的值
        double v00 = 0.0, v01 = 0.0, v10 = 0.0, v11 = 0.0;
        band->RasterIO(GF_Read, x0, y0, 1, 1, &v00, 1, 1, GDT_Float64, 0, 0);
        band->RasterIO(GF_Read, x1, y0, 1, 1, &v01, 1, 1, GDT_Float64, 0, 0);
        band->RasterIO(GF_Read, x0, y1, 1, 1, &v10, 1, 1, GDT_Float64, 0, 0);
        band->RasterIO(GF_Read, x1, y1, 1, 1, &v11, 1, 1, GDT_Float64, 0, 0);

        // 双线性插值
        double fx = x - x0;
        double fy = y - y0;

        double value = v00 * (1 - fx) * (1 - fy) +
            v01 * fx * (1 - fy) +
            v10 * (1 - fx) * fy +
            v11 * fx * fy;

        return value;
    }

    /**
     * 应用PCA融合到单个块
     */
    static std::vector<std::vector<double>> applyPCAFusion(const std::vector<double>& panData,
        const std::vector<std::vector<double>>& msData,
        int numPixels, const PCAParameters& params) {

        int numBands = params.numBands;

        // 前向PCA变换
        std::vector<std::vector<double>> pcData(numBands, std::vector<double>(numPixels));
        for (int pc = 0; pc < numBands; pc++) {
            for (int p = 0; p < numPixels; p++) {
                double pcValue = 0.0;
                for (int b = 0; b < numBands; b++) {
                    pcValue += params.eigenVectors[pc][b] * (msData[b][p] - params.means[b]);
                }
                pcData[pc][p] = pcValue;
            }
        }

        // 用标准化的全色影像替换第一主成分
        for (int p = 0; p < numPixels; p++) {
            double normalizedPan = (panData[p] - params.panMean) * (params.pc1Std / params.panStd) + params.pc1Mean;
            pcData[0][p] = normalizedPan;
        }

        // 逆PCA变换
        std::vector<std::vector<double>> fusedData(numBands, std::vector<double>(numPixels));
        for (int b = 0; b < numBands; b++) {
            for (int p = 0; p < numPixels; p++) {
                double bandValue = params.means[b];
                for (int pc = 0; pc < numBands; pc++) {
                    bandValue += params.eigenVectors[pc][b] * pcData[pc][p];
                }
                fusedData[b][p] = bandValue;
            }
        }

        return fusedData;
    }

    // 继续保留原有的评估方法，但确保没有ImageInfo类型冲突
    static double calculateEntropy(const std::vector<std::vector<double>>& samples) {
        if (samples.empty()) return 0.0;

        std::vector<int> histogram(256, 0);

        double minVal = samples[0][0], maxVal = samples[0][0];
        for (const auto& sample : samples) {
            if (sample[0] < minVal) minVal = sample[0];
            if (sample[0] > maxVal) maxVal = sample[0];
        }

        double range = maxVal - minVal;
        if (range < 1e-10) return 0.0;

        for (const auto& sample : samples) {
            int bin = (int)((sample[0] - minVal) / range * 255);
            bin = std::max(0, std::min(255, bin));
            histogram[bin]++;
        }

        double entropy = 0.0;
        int totalSamples = samples.size();
        for (int i = 0; i < 256; i++) {
            if (histogram[i] > 0) {
                double prob = (double)histogram[i] / totalSamples;
                entropy -= prob * std::log2(prob);
            }
        }

        return entropy;
    }

    static double calculateCorrelation(const std::vector<std::vector<double>>& orig,
        const std::vector<std::vector<double>>& fused) {
        if (orig.empty() || fused.empty()) return 0.0;

        int numBands = orig[0].size();
        double totalCorr = 0.0;
        int validBands = 0;

        for (int b = 0; b < numBands; b++) {
            double meanOrig = 0.0, meanFused = 0.0;
            for (size_t i = 0; i < orig.size(); i++) {
                meanOrig += orig[i][b];
                meanFused += fused[i][b];
            }
            meanOrig /= orig.size();
            meanFused /= fused.size();

            double numerator = 0.0, denomOrig = 0.0, denomFused = 0.0;
            for (size_t i = 0; i < orig.size(); i++) {
                double diffOrig = orig[i][b] - meanOrig;
                double diffFused = fused[i][b] - meanFused;
                numerator += diffOrig * diffFused;
                denomOrig += diffOrig * diffOrig;
                denomFused += diffFused * diffFused;
            }

            double correlation = 0.0;
            if (denomOrig > 1e-10 && denomFused > 1e-10) {
                correlation = numerator / std::sqrt(denomOrig * denomFused);
                totalCorr += correlation;
                validBands++;
            }
        }

        return validBands > 0 ? totalCorr / validBands : 0.0;
    }

    static double calculateRMSE(const std::vector<std::vector<double>>& orig,
        const std::vector<std::vector<double>>& fused) {
        if (orig.empty() || fused.empty()) return 0.0;

        double sumSquaredError = 0.0;
        int totalElements = 0;

        for (size_t i = 0; i < orig.size(); i++) {
            for (size_t b = 0; b < orig[i].size(); b++) {
                double diff = orig[i][b] - fused[i][b];
                sumSquaredError += diff * diff;
                totalElements++;
            }
        }

        return std::sqrt(sumSquaredError / totalElements);
    }

    static double calculateCrossEntropy(const std::vector<std::vector<double>>& orig,
        const std::vector<std::vector<double>>& fused) {
        if (orig.empty() || fused.empty()) return 0.0;

        std::vector<int> histOrig(256, 0), histFused(256, 0);

        double minVal = orig[0][0], maxVal = orig[0][0];
        for (const auto& sample : orig) {
            for (double val : sample) {
                if (val < minVal) minVal = val;
                if (val > maxVal) maxVal = val;
            }
        }

        double range = maxVal - minVal;
        if (range < 1e-10) return 0.0;

        int totalSamples = 0;
        for (size_t i = 0; i < orig.size(); i++) {
            for (size_t b = 0; b < orig[i].size(); b++) {
                int binOrig = (int)((orig[i][b] - minVal) / range * 255);
                int binFused = (int)((fused[i][b] - minVal) / range * 255);
                binOrig = std::max(0, std::min(255, binOrig));
                binFused = std::max(0, std::min(255, binFused));
                histOrig[binOrig]++;
                histFused[binFused]++;
                totalSamples++;
            }
        }

        double crossEntropy = 0.0;
        for (int i = 0; i < 256; i++) {
            if (histOrig[i] > 0 && histFused[i] > 0) {
                double pOrig = (double)histOrig[i] / totalSamples;
                double pFused = (double)histFused[i] / totalSamples;
                crossEntropy -= pOrig * std::log2(pFused + 1e-10);
            }
        }

        return crossEntropy;
    }

    static double calculatePSNR(const std::vector<std::vector<double>>& orig,
        const std::vector<std::vector<double>>& fused) {
        double rmse = calculateRMSE(orig, fused);
        if (rmse < 1e-10) {
            return 999.99;
        }

        double maxVal = 65535.0;

        double actualMax = 0.0;
        for (const auto& sample : orig) {
            for (double val : sample) {
                if (val > actualMax) actualMax = val;
            }
        }

        if (actualMax <= 1.0) {
            maxVal = 1.0;
        }

        return 20.0 * std::log10(maxVal / rmse);
    }

    // 输出评估结果
    static void printEvaluationResults(const EvaluationResults& results) {
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "\n熵值: " << results.entropy << std::endl;
        std::cout << "相关系数: " << results.correlation_coefficient << std::endl;
        std::cout << "均方根误差: " << results.rmse << std::endl;
        std::cout << "交叉熵: " << results.cross_entropy << std::endl;
        std::cout << "峰值信噪比: " << results.psnr << std::endl;
    }
};