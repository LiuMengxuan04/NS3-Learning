# NS3-Learning: 数据中心网络仿真学习项目

[![NS-3 Version](https://img.shields.io/badge/NS--3-3.44-blue.svg)](https://www.nsnam.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

## 📖 项目介绍

这是一个基于 **NS-3 (Network Simulator 3)** 的数据中心网络仿真学习项目，主要聚焦于 **Fat-Tree 拓扑** 的实现和性能分析。

本项目包含了多种 Fat-Tree 实现方案的对比研究，包括：
- 基于全局 ECMP 路由的标准实现
- 基于静态路由聚合的改进实现
- 完整的性能测试和可视化分析

## 🏗️ 项目结构

```
ns3-learning/
├── Fat-Tree/                          # Fat-Tree 相关实现
│   ├── DCN_FatTree_CSMA.cc           # ECMP 版本 (全局路由)
│   ├── DCN_FatTree_Custom.cc         # 静态路由版本 (路由聚合)
│   ├── DCN_FatTree_代码讲解.md         # ECMP 版本详细讲解
│   └── DCN_FatTree_Custom_代码讲解.md  # 静态路由版本详细讲解
├── README.md                          # 项目说明 (中文)
└── README.en.md                       # 项目说明 (英文)
```

## 🚀 快速开始

### 环境要求

- **NS-3**: 版本 3.44
- **编译器**: GCC 7.0+ 或 Clang 6.0+
- **操作系统**: Linux (推荐 Ubuntu 18.04+)
- **依赖包**: Python 3.6+, SQLite3, etc.

### 安装步骤

1. **下载并编译 NS-3**
   ```bash
   cd /path/to/ns3
   ./ns3 configure --enable-examples --enable-tests
   ./ns3 build
   ```

2. **克隆本项目**
   ```bash
   git clone https://gitee.com/your-username/ns3-learning.git
   cd ns3-learning
   ```

3. **运行仿真**
   ```bash
   # ECMP 版本 (推荐)
   ./ns3 run DCN_FatTree_CSMA

   # 静态路由版本
   ./ns3 run DCN_FatTree_Custom
   ```

## 📊 核心特性

### Fat-Tree 实现对比

| 特性 | ECMP 版本 | 静态路由版本 |
|------|----------|-------------|
| **路由算法** | 全局 ECMP | 静态路由聚合 |
| **IP 分配** | 规律化 /30 子网 | 改进版 /30 子网 |
| **性能** | 更优延迟 | 路由表紧凑 |
| **复杂度** | 中等 | 中等偏高 |
| **适用场景** | 生产仿真 | 研究学习 |

### 仿真结果

- ✅ **连通性**: 4 个 Pod 完全连通
- ✅ **丢包率**: 0%
- ✅ **延迟**: 微秒级 (< 50μs)
- ✅ **ECMP 负载均衡**: 流量自动分布到多路径

## 📚 学习资源

### 核心文档

- [**Fat-Tree ECMP 版本详解**](Fat-Tree/DCN_FatTree_代码讲解.md)
- [**Fat-Tree 静态路由版本详解**](Fat-Tree/DCN_FatTree_Custom_代码讲解.md)

### 关键概念

- **Fat-Tree 拓扑**: k=4 的三层架构 (k/2)^2 = 4 个核心交换机
- **ECMP 路由**: 等价多路径负载均衡
- **FlowMonitor**: NS-3 流量监控和统计
- **NetAnim**: 网络动画可视化

## 🔧 使用说明

### 运行参数

```bash
# 禁用 ECMP (仅 ECMP 版本支持)
./ns3 run "DCN_FatTree_CSMA --ECMProuting=false"

# 使用 GDB 调试
./ns3 run DCN_FatTree_CSMA --gdb

# 查看详细日志
NS_LOG="DCN_FatTree_Simulation=level_info" ./ns3 run DCN_FatTree_CSMA
```

### 输出文件

- `*.flowmon`: FlowMonitor 统计数据
- `animation.xml`: NetAnim 可视化文件
- `*.pcap`: Wireshark 数据包文件

## 🤝 参与贡献

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 Pull Request

## 📄 许可证

本项目采用 MIT 许可证 - 查看 [LICENSE](LICENSE) 文件了解详情

## 👨‍💻 作者

**Ceylan Liu** - *初始开发*
- Email: 1575373800@qq.com
- Gitee: [ceylanliu](https://gitee.com/ceylanliu)

## 🙏 致谢

- [NS-3 官方文档](https://www.nsnam.org/documentation/)
- [Fat-Tree 原始论文](https://ccr.sigcomm.org/online/files/p63-alfares.pdf)
- Gitee 平台提供代码托管服务

---

⭐ 如果这个项目对你有帮助，请给它一个 star！
