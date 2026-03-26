GEOPROCESSer - 高性能遥感影像处理系统
GEOPROCESSer 是一款基于 C++ 17 和 Qt 6 开发的专业遥感影像处理软件。它深度集成了 GDAL 引擎，专为处理超大比例尺卫星数据而设计，提供从影像管理、交互式几何配准到高精度影像融合的全流程解决方案。

核心特性
1. 超大影像智能显示引擎 (Tiled Engine)
智能瓦片加载：针对超过 2GB 的超大型影像，自动启用分块按需加载机制，显著降低内存占用，确保普通配置电脑也能流畅操作。
多线程渲染：支持并发瓦片加载，提供极速的缩放与平移体验。
采样优化：内置双线性插值算法，保证在任何缩放级别下影像均能清晰呈现。
2. 交互式几何配准 (Geometric Registration)
精密控制点管理：支持手动拾取、移动及删除控制点，具备像素级拾取精度与容差管理。
文件互操作：控制点数据支持 .txt 格式的导入与导出，方便在不同平台间迁移。
多种配准模型：内置线性变换、多项式变换及样条函数模型，满足不同地形形变的校正需求。
3. 高性能影像融合 (Image Fusion)
针对大数据量进行了分块算法优化，支持主流空谱融合方案：
IHS 融合：采用强度-色调-饱和度变换，集成直方图匹配技术。
改进型 PCA 融合：基于主成分分析，支持自定义采样比例，并内置精度评估系统（提供熵值、相关系数、RMSE、PSNR 等科学指标）。
4. 完善的工程管理系统
rsp 工程文件：基于 JSON 格式序列化项目状态，支持记录影像元数据、波段组合及视图位置。
最近工程管理：自动记录历史项目，支持一键快速恢复工作现场。
技术栈
GUI 框架: Qt 6.x (C++)
影像引擎: GDAL (Geospatial Data Abstraction Library)
数值计算: Eigen 3, OpenCV
开发环境: Visual Studio 2022 / MSVC x64
环境依赖
Qt 6 (QtWidgets, QtGui, QtCore)
GDAL 3.x
Eigen 3 & OpenCV 4.x
编译运行
克隆仓库：git clone [https://github.com/Iris762/GEOPROCESSer.git](https://github.com/Iris762/GEOPROCESSer.git)
使用 Visual Studio 2022 打开 projectssa.sln。
确保所有依赖库（.dll）已放置在生成的 .exe 同级目录下。
直接运行项目，程序将自动执行 GDAL 驱动环境自检。
开源协议
本项目采用 MIT License 协议开源。

开发者: Iris 

项目地址: https://github.com/Iris762/GEOPROCESSer
