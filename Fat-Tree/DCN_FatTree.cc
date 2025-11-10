/*
 * ============================================================================
 * 标题: 基于 ns-3 的数据中心网络 (DCN) Fat-Tree 拓扑仿真
 * ============================================================================
 * 
 * 描述:
 *   本程序实现了一个 k=4 的 Fat-Tree 数据中心网络架构，包含:
 *   - 16 台服务器 (分布在 4 个 Pod 中，每个 Pod 4 台)
 *   - 16 个接入/汇聚层交换机 (每个 Pod 4 个交换机)
 *   - 4 个核心层交换机
 *   
 *   功能特性:
 *   - 使用 ECMP (等价多路径) 路由实现负载均衡
 *   - 支持 UDP 和 TCP 流量测试
 *   - 生成 FlowMonitor 统计数据 (吞吐量、延迟、丢包率等)
 *   - 生成 NetAnim 可视化动画文件
 *   - 捕获 PCAP 数据包用于 Wireshark 分析
 *
 * IP 地址分配规则:
 *   - Pod 内链路: 10.PodID.SubnetID.0/30
 *     例如: Pod 0 使用 10.0.0.0/30 ~ 10.0.7.0/30
 *   - 核心层链路: 10.10.SubnetID.0/30
 *     例如: 10.10.0.0/30 ~ 10.10.15.0/30
 *
 * 拓扑结构:
 *   每个 Pod 包含:
 *     - 4 台服务器 (节点 0-3)
 *     - 2 个接入层交换机 (节点 4-5)
 *     - 2 个汇聚层交换机 (节点 6-7)
 *   
 *   连接关系:
 *     - 每个接入交换机连接 2 台服务器
 *     - 接入交换机与汇聚交换机全连接 (2x2=4 条链路)
 *     - 每个汇聚交换机连接所有核心交换机 (2x4=8 条链路/Pod)
 *
 * 原作者: Amit Khandu Bhalerao (2016-12-17)
 * 修改者: [Liu Mengxuan] (2025-11-09)
 * 版本: 2.0
 * ns-3 版本: 3.44
 * ============================================================================
 */

// ============================================================================
// 头文件引入
// ============================================================================
#include "ns3/core-module.h"              // 核心模块 (时间、日志、命令行等)
#include "ns3/network-module.h"           // 网络模块 (节点、设备等)
#include "ns3/internet-module.h"          // 互联网协议栈 (TCP/IP)
#include "ns3/point-to-point-module.h"    // 点对点链路
#include "ns3/applications-module.h"      // 应用层 (Echo, BulkSend 等)
#include "ns3/ipv4-list-routing-helper.h" // 路由列表助手
#include "ns3/ipv4-static-routing-helper.h" // 静态路由助手
#include "ns3/mobility-module.h"          // 移动性模型 (用于 NetAnim 定位)
#include "ns3/netanim-module.h"           // NetAnim 动画生成
#include "ns3/ipv4-global-routing-helper.h" // 全局路由助手 (ECMP)
#include "ns3/flow-monitor-helper.h"      // 流量监控助手

using namespace ns3;
using namespace std;

// 定义日志组件，用于调试输出
NS_LOG_COMPONENT_DEFINE("DCN_FatTree_Simulation");

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char *argv[])
{
	// ========================================================================
	// 1. 配置模拟参数
	// ========================================================================
	
	// 1.1 命令行参数解析
	CommandLine cmd;
	bool ECMProuting = true; // 默认启用 ECMP (等价多路径) 路由
	cmd.AddValue("ECMProuting", "Enable ECMP routing (true/false)", ECMProuting);
	cmd.Parse(argc, argv);
	
	// 1.2 设置时间精度为纳秒级别 (数据中心网络需要高精度)
	Time::SetResolution(Time::NS);
	
	// 1.3 启用应用层日志输出，便于调试和观察数据包收发
	LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
	LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
	
	// 1.4 配置全局路由参数
	// ECMP 路由: 当存在多条等价路径时，随机选择一条路径进行负载均衡
	Config::SetDefault("ns3::Ipv4GlobalRouting::RandomEcmpRouting", BooleanValue(ECMProuting));
	
	// 1.5 定义队列大小参数
	// 队列大小影响缓冲能力和延迟，需要根据链路带宽和延迟特性调整
	uint32_t Corequeuesize, Leafqueuesize;
	Corequeuesize = 8;  // 核心层交换机队列: 8 个数据包
	Leafqueuesize = 4;  // 接入/汇聚层交换机队列: 4 个数据包

	// ========================================================================
	// 2. 定义链路助手 (配置不同层级的链路特性)
	// ========================================================================
	
	// 2.1 服务器到接入交换机链路 (边缘链路)
	// 特点: 带宽较低 (10Gbps)，延迟较高 (200ns)
	PointToPointHelper NodeToSW;
	NodeToSW.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
	NodeToSW.SetChannelAttribute("Delay", StringValue("200ns"));
	// 注意: 此链路不设置队列大小，使用默认值

	// 2.2 汇聚层到核心层交换机链路 (上行链路)
	// 特点: 高带宽 (40Gbps)，低延迟 (50ns)，较大队列
	PointToPointHelper SWToSW_50ns;
	SWToSW_50ns.SetDeviceAttribute("DataRate", StringValue("40Gbps"));
	SWToSW_50ns.SetChannelAttribute("Delay", StringValue("50ns"));
	// 使用 DropTailQueue: 当队列满时丢弃新到达的数据包 (尾部丢弃策略)
	SWToSW_50ns.SetQueue("ns3::DropTailQueue", "MaxSize", 
	                     StringValue(std::to_string(Corequeuesize) + "p")); 
	
	// 2.3 接入层到汇聚层交换机链路 (中间链路)
	// 特点: 高带宽 (40Gbps)，中等延迟 (70ns)，中等队列
	PointToPointHelper SWToSW_70ns;
	SWToSW_70ns.SetDeviceAttribute("DataRate", StringValue("40Gbps"));
	SWToSW_70ns.SetChannelAttribute("Delay", StringValue("70ns"));
	SWToSW_70ns.SetQueue("ns3::DropTailQueue", "MaxSize", 
	                     StringValue(std::to_string(Leafqueuesize) + "p")); 
	
	// ========================================================================
	// 3. 创建节点 (k=4 Fat-Tree 拓扑)
	// ========================================================================
	
	// 3.1 创建 Pod 节点容器
	// k=4 Fat-Tree 包含 4 个 Pod，每个 Pod 有 8 个节点:
	//   - 节点 0-3: 服务器 (Server 0-3)
	//   - 节点 4-5: 接入层交换机 (Access Switch 4-5)
	//   - 节点 6-7: 汇聚层交换机 (Aggregation Switch 6-7)
	NodeContainer pod0, pod1, pod2, pod3, core;
	pod0.Create(8); // Pod 0
	pod1.Create(8); // Pod 1
	pod2.Create(8); // Pod 2
	pod3.Create(8); // Pod 3
	
	// 3.2 创建核心层交换机
	// k=4 Fat-Tree 需要 (k/2)^2 = 4 个核心交换机
	core.Create(4);

	// 3.3 为所有节点设置移动性模型
	// 虽然数据中心节点是静态的，但 NetAnim 需要位置信息来渲染动画
	// 使用 ConstantPositionMobilityModel 表示节点位置固定不变
	MobilityHelper mobility;
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	mobility.Install(pod0);
	mobility.Install(pod1);
	mobility.Install(pod2);
	mobility.Install(pod3);
	mobility.Install(core);
	
	// 3.4 安装互联网协议栈 (TCP/IP)
	// 为所有节点安装完整的 TCP/IP 协议栈，包括:
	//   - IPv4 协议
	//   - TCP/UDP 传输层协议
	//   - 全局路由协议 (用于 ECMP)
	InternetStackHelper stack;
	stack.Install(pod0);
	stack.Install(pod1);
	stack.Install(pod2);
	stack.Install(pod3);
	stack.Install(core);
	
	
	// ========================================================================
	// 4. 构建拓扑：连接节点 (创建物理链路)
	// ========================================================================
	
	// 4.1 Pod 0 内部连接
	// 拓扑结构:
	//   Server0,1 -> AccessSW4 -> AggrSW6,7
	//   Server2,3 -> AccessSW5 -> AggrSW6,7
	NetDeviceContainer pod0_dev, pod0_dev2, pod0_dev3, pod0_dev4;
	NetDeviceContainer pod0_dev5, pod0_dev6, pod0_dev7, pod0_dev8;
	
	// 服务器到接入交换机 (边缘层)
	pod0_dev  = NodeToSW.Install(pod0.Get(0), pod0.Get(4)); // Server0 -> AccessSW4
	pod0_dev2 = NodeToSW.Install(pod0.Get(1), pod0.Get(4)); // Server1 -> AccessSW4
	pod0_dev3 = NodeToSW.Install(pod0.Get(2), pod0.Get(5)); // Server2 -> AccessSW5
	pod0_dev4 = NodeToSW.Install(pod0.Get(3), pod0.Get(5)); // Server3 -> AccessSW5
	
	// 接入交换机到汇聚交换机 (全连接，提供冗余路径)
	pod0_dev5 = SWToSW_50ns.Install(pod0.Get(4), pod0.Get(6)); // AccessSW4 -> AggrSW6
	pod0_dev6 = SWToSW_70ns.Install(pod0.Get(4), pod0.Get(7)); // AccessSW4 -> AggrSW7
	pod0_dev7 = SWToSW_50ns.Install(pod0.Get(5), pod0.Get(6)); // AccessSW5 -> AggrSW6
	pod0_dev8 = SWToSW_70ns.Install(pod0.Get(5), pod0.Get(7)); // AccessSW5 -> AggrSW7
	
	// 4.2 Pod 1 内部连接 (结构同 Pod 0)
	NetDeviceContainer pod1_dev, pod1_dev2, pod1_dev3, pod1_dev4;
	NetDeviceContainer pod1_dev5, pod1_dev6, pod1_dev7, pod1_dev8;
	pod1_dev  = NodeToSW.Install(pod1.Get(0), pod1.Get(4));
	pod1_dev2 = NodeToSW.Install(pod1.Get(1), pod1.Get(4));
	pod1_dev3 = NodeToSW.Install(pod1.Get(2), pod1.Get(5));
	pod1_dev4 = NodeToSW.Install(pod1.Get(3), pod1.Get(5));
	pod1_dev5 = SWToSW_50ns.Install(pod1.Get(4), pod1.Get(6));
	pod1_dev6 = SWToSW_70ns.Install(pod1.Get(4), pod1.Get(7));
	pod1_dev7 = SWToSW_50ns.Install(pod1.Get(5), pod1.Get(6));
	pod1_dev8 = SWToSW_70ns.Install(pod1.Get(5), pod1.Get(7));
	
	// 4.3 Pod 2 内部连接 (结构同 Pod 0)
	NetDeviceContainer pod2_dev, pod2_dev2, pod2_dev3, pod2_dev4;
	NetDeviceContainer pod2_dev5, pod2_dev6, pod2_dev7, pod2_dev8;
	pod2_dev  = NodeToSW.Install(pod2.Get(0), pod2.Get(4));
	pod2_dev2 = NodeToSW.Install(pod2.Get(1), pod2.Get(4));
	pod2_dev3 = NodeToSW.Install(pod2.Get(2), pod2.Get(5));
	pod2_dev4 = NodeToSW.Install(pod2.Get(3), pod2.Get(5));
	pod2_dev5 = SWToSW_50ns.Install(pod2.Get(4), pod2.Get(6));
	pod2_dev6 = SWToSW_70ns.Install(pod2.Get(4), pod2.Get(7));
	pod2_dev7 = SWToSW_50ns.Install(pod2.Get(5), pod2.Get(6));
	pod2_dev8 = SWToSW_70ns.Install(pod2.Get(5), pod2.Get(7));
	
	// 4.4 Pod 3 内部连接 (结构同 Pod 0)
	NetDeviceContainer pod3_dev, pod3_dev2, pod3_dev3, pod3_dev4;
	NetDeviceContainer pod3_dev5, pod3_dev6, pod3_dev7, pod3_dev8;
	pod3_dev  = NodeToSW.Install(pod3.Get(0), pod3.Get(4));
	pod3_dev2 = NodeToSW.Install(pod3.Get(1), pod3.Get(4));
	pod3_dev3 = NodeToSW.Install(pod3.Get(2), pod3.Get(5));
	pod3_dev4 = NodeToSW.Install(pod3.Get(3), pod3.Get(5));
	pod3_dev5 = SWToSW_50ns.Install(pod3.Get(4), pod3.Get(6));
	pod3_dev6 = SWToSW_70ns.Install(pod3.Get(4), pod3.Get(7));
	pod3_dev7 = SWToSW_50ns.Install(pod3.Get(5), pod3.Get(6));
	pod3_dev8 = SWToSW_70ns.Install(pod3.Get(5), pod3.Get(7));
	
	// 4.5 汇聚层到核心层连接 (跨 Pod 通信路径)
	// 每个 Pod 的每个汇聚交换机都连接到所有核心交换机
	// 这提供了 Pod 间通信的多条等价路径，是 ECMP 的基础
	NetDeviceContainer core_dev, core_dev2, core_dev3, core_dev4;
	NetDeviceContainer core_dev5, core_dev6, core_dev7, core_dev8;
	NetDeviceContainer core_dev9, core_dev10, core_dev11, core_dev12;
	NetDeviceContainer core_dev13, core_dev14, core_dev15, core_dev16;
	
	// 所有 Pod 的 AggrSW6 连接到 Core0
	core_dev  = SWToSW_50ns.Install(pod0.Get(6), core.Get(0));
	core_dev2 = SWToSW_50ns.Install(pod1.Get(6), core.Get(0));
	core_dev3 = SWToSW_50ns.Install(pod2.Get(6), core.Get(0));
	core_dev4 = SWToSW_50ns.Install(pod3.Get(6), core.Get(0));
	
	// 所有 Pod 的 AggrSW6 连接到 Core1
	core_dev5 = SWToSW_50ns.Install(pod0.Get(6), core.Get(1));
	core_dev6 = SWToSW_50ns.Install(pod1.Get(6), core.Get(1));
	core_dev7 = SWToSW_50ns.Install(pod2.Get(6), core.Get(1));
	core_dev8 = SWToSW_50ns.Install(pod3.Get(6), core.Get(1));
	
	// 所有 Pod 的 AggrSW7 连接到 Core2
	core_dev9  = SWToSW_50ns.Install(pod0.Get(7), core.Get(2));
	core_dev10 = SWToSW_50ns.Install(pod1.Get(7), core.Get(2));
	core_dev11 = SWToSW_50ns.Install(pod2.Get(7), core.Get(2));
	core_dev12 = SWToSW_50ns.Install(pod3.Get(7), core.Get(2));
	
	// 所有 Pod 的 AggrSW7 连接到 Core3
	core_dev13 = SWToSW_50ns.Install(pod0.Get(7), core.Get(3));
	core_dev14 = SWToSW_50ns.Install(pod1.Get(7), core.Get(3));
	core_dev15 = SWToSW_50ns.Install(pod2.Get(7), core.Get(3));
	core_dev16 = SWToSW_50ns.Install(pod3.Get(7), core.Get(3));
		
	// --- 5. 分配 IP 地址 (使用规律化的 P2P 独立子网方案) ---
	// IP 地址分配规则: 10.Pod.Subnet.0/30
	// Pod 0: 10.0.x.0/30  (x=0~7 for pod内链路)
	// Pod 1: 10.1.x.0/30
	// Pod 2: 10.2.x.0/30
	// Pod 3: 10.3.x.0/30
	// Core:  10.10.x.0/30 (x=0~15 for 16条核心链路)
	
	Ipv4AddressHelper address;

	// === Pod 0: 10.0.x.0/30 ===
	Ipv4InterfaceContainer pod0_Iface,pod0_Iface2,pod0_Iface3,pod0_Iface4,pod0_Iface5,pod0_Iface6,pod0_Iface7,pod0_Iface8;
	address.SetBase("10.0.0.0","255.255.255.252");  // Server0-AccessSW4
	pod0_Iface = address.Assign (pod0_dev);
	address.SetBase("10.0.1.0","255.255.255.252");  // Server1-AccessSW4
	pod0_Iface2 = address.Assign(pod0_dev2);
	address.SetBase("10.0.2.0","255.255.255.252");  // Server2-AccessSW5
	pod0_Iface3 = address.Assign(pod0_dev3);
	address.SetBase("10.0.3.0","255.255.255.252");  // Server3-AccessSW5
	pod0_Iface4 = address.Assign(pod0_dev4);
	address.SetBase("10.0.4.0","255.255.255.252");  // AccessSW4-AggrSW6
	pod0_Iface5 = address.Assign(pod0_dev5);
	address.SetBase("10.0.5.0","255.255.255.252");  // AccessSW4-AggrSW7
	pod0_Iface6 = address.Assign(pod0_dev6);
	address.SetBase("10.0.6.0","255.255.255.252");  // AccessSW5-AggrSW6
	pod0_Iface7 = address.Assign(pod0_dev7);
	address.SetBase("10.0.7.0","255.255.255.252");  // AccessSW5-AggrSW7
	pod0_Iface8 = address.Assign(pod0_dev8);

	// === Pod 1: 10.1.x.0/30 ===
	Ipv4InterfaceContainer pod1_Iface,pod1_Iface2,pod1_Iface3,pod1_Iface4,pod1_Iface5,pod1_Iface6,pod1_Iface7,pod1_Iface8;
	address.SetBase("10.1.0.0","255.255.255.252");  // Server0-AccessSW4
	pod1_Iface = address.Assign (pod1_dev);
	address.SetBase("10.1.1.0","255.255.255.252");  // Server1-AccessSW4
	pod1_Iface2 = address.Assign(pod1_dev2);
	address.SetBase("10.1.2.0","255.255.255.252");  // Server2-AccessSW5
	pod1_Iface3 = address.Assign(pod1_dev3);
	address.SetBase("10.1.3.0","255.255.255.252");  // Server3-AccessSW5
	pod1_Iface4 = address.Assign(pod1_dev4);
	address.SetBase("10.1.4.0","255.255.255.252");  // AccessSW4-AggrSW6
	pod1_Iface5 = address.Assign(pod1_dev5);
	address.SetBase("10.1.5.0","255.255.255.252");  // AccessSW4-AggrSW7
	pod1_Iface6 = address.Assign(pod1_dev6);
	address.SetBase("10.1.6.0","255.255.255.252");  // AccessSW5-AggrSW6
	pod1_Iface7 = address.Assign(pod1_dev7);
	address.SetBase("10.1.7.0","255.255.255.252");  // AccessSW5-AggrSW7
	pod1_Iface8 = address.Assign(pod1_dev8);

	// === Pod 2: 10.2.x.0/30 ===
	Ipv4InterfaceContainer pod2_Iface,pod2_Iface2,pod2_Iface3,pod2_Iface4,pod2_Iface5,pod2_Iface6,pod2_Iface7,pod2_Iface8;
	address.SetBase("10.2.0.0","255.255.255.252");  // Server0-AccessSW4
	pod2_Iface = address.Assign (pod2_dev);
	address.SetBase("10.2.1.0","255.255.255.252");  // Server1-AccessSW4
	pod2_Iface2 = address.Assign(pod2_dev2);
	address.SetBase("10.2.2.0","255.255.255.252");  // Server2-AccessSW5
	pod2_Iface3 = address.Assign(pod2_dev3);
	address.SetBase("10.2.3.0","255.255.255.252");  // Server3-AccessSW5
	pod2_Iface4 = address.Assign(pod2_dev4);
	address.SetBase("10.2.4.0","255.255.255.252");  // AccessSW4-AggrSW6
	pod2_Iface5 = address.Assign(pod2_dev5);
	address.SetBase("10.2.5.0","255.255.255.252");  // AccessSW4-AggrSW7
	pod2_Iface6 = address.Assign(pod2_dev6);
	address.SetBase("10.2.6.0","255.255.255.252");  // AccessSW5-AggrSW6
	pod2_Iface7 = address.Assign(pod2_dev7);
	address.SetBase("10.2.7.0","255.255.255.252");  // AccessSW5-AggrSW7
	pod2_Iface8 = address.Assign(pod2_dev8);

	// === Pod 3: 10.3.x.0/30 ===
	Ipv4InterfaceContainer pod3_Iface,pod3_Iface2,pod3_Iface3,pod3_Iface4,pod3_Iface5,pod3_Iface6,pod3_Iface7,pod3_Iface8;
	address.SetBase("10.3.0.0","255.255.255.252");  // Server0-AccessSW4
	pod3_Iface = address.Assign (pod3_dev);
	address.SetBase("10.3.1.0","255.255.255.252");  // Server1-AccessSW4
	pod3_Iface2 = address.Assign(pod3_dev2);
	address.SetBase("10.3.2.0","255.255.255.252");  // Server2-AccessSW5
	pod3_Iface3 = address.Assign(pod3_dev3);
	address.SetBase("10.3.3.0","255.255.255.252");  // Server3-AccessSW5
	pod3_Iface4 = address.Assign(pod3_dev4);
	address.SetBase("10.3.4.0","255.255.255.252");  // AccessSW4-AggrSW6
	pod3_Iface5 = address.Assign(pod3_dev5);
	address.SetBase("10.3.5.0","255.255.255.252");  // AccessSW4-AggrSW7
	pod3_Iface6 = address.Assign(pod3_dev6);
	address.SetBase("10.3.6.0","255.255.255.252");  // AccessSW5-AggrSW6
	pod3_Iface7 = address.Assign(pod3_dev7);
	address.SetBase("10.3.7.0","255.255.255.252");  // AccessSW5-AggrSW7
	pod3_Iface8 = address.Assign(pod3_dev8);
	
	// === 核心层链路: 10.10.x.0/30 ===
	// 使用 10.10.x.0 来表示核心链路，便于识别
	Ipv4InterfaceContainer core_Iface,core_Iface2,core_Iface3,core_Iface4,core_Iface5,core_Iface6,core_Iface7,core_Iface8;
	Ipv4InterfaceContainer core_Iface9,core_Iface10,core_Iface11,core_Iface12,core_Iface13,core_Iface14,core_Iface15,core_Iface16;
	address.SetBase("10.10.0.0","255.255.255.252");   // Pod0.AggrSW6-Core0
	core_Iface = address.Assign(core_dev);
	address.SetBase("10.10.1.0","255.255.255.252");   // Pod1.AggrSW6-Core0
	core_Iface2 = address.Assign(core_dev2);
	address.SetBase("10.10.2.0","255.255.255.252");   // Pod2.AggrSW6-Core0
	core_Iface3 = address.Assign(core_dev3);
	address.SetBase("10.10.3.0","255.255.255.252");   // Pod3.AggrSW6-Core0
	core_Iface4 = address.Assign(core_dev4);
	address.SetBase("10.10.4.0","255.255.255.252");   // Pod0.AggrSW6-Core1
	core_Iface5 = address.Assign(core_dev5);
	address.SetBase("10.10.5.0","255.255.255.252");   // Pod1.AggrSW6-Core1
	core_Iface6 = address.Assign(core_dev6);
	address.SetBase("10.10.6.0","255.255.255.252");   // Pod2.AggrSW6-Core1
	core_Iface7 = address.Assign(core_dev7);
	address.SetBase("10.10.7.0","255.255.255.252");   // Pod3.AggrSW6-Core1
	core_Iface8 = address.Assign(core_dev8);
	address.SetBase("10.10.8.0","255.255.255.252");   // Pod0.AggrSW7-Core2
	core_Iface9 = address.Assign(core_dev9);
	address.SetBase("10.10.9.0","255.255.255.252");   // Pod1.AggrSW7-Core2
	core_Iface10 = address.Assign(core_dev10);
	address.SetBase("10.10.10.0","255.255.255.252");  // Pod2.AggrSW7-Core2
	core_Iface11 = address.Assign(core_dev11);
	address.SetBase("10.10.11.0","255.255.255.252");  // Pod3.AggrSW7-Core2
	core_Iface12 = address.Assign(core_dev12);
	address.SetBase("10.10.12.0","255.255.255.252");  // Pod0.AggrSW7-Core3
	core_Iface13 = address.Assign(core_dev13);
	address.SetBase("10.10.13.0","255.255.255.252");  // Pod1.AggrSW7-Core3
	core_Iface14 = address.Assign(core_dev14);
	address.SetBase("10.10.14.0","255.255.255.252");  // Pod2.AggrSW7-Core3
	core_Iface15 = address.Assign(core_dev15);
	address.SetBase("10.10.15.0","255.255.255.252");  // Pod3.AggrSW7-Core3
	core_Iface16 = address.Assign(core_dev16);
	
	// ========================================================================
	// 6. 计算并填充路由表
	// ========================================================================
	
	// 使用全局路由助手计算所有节点之间的最短路径
	// 这会运行 Dijkstra 算法，并在每个节点上填充路由表
	// 当启用 ECMP 时，会自动识别等价路径并进行负载均衡
	Ipv4GlobalRoutingHelper::PopulateRoutingTables();
	
	// ========================================================================
	// 7. 部署应用程序 (生成测试流量)
	// ========================================================================

	// 7.1 UDP Echo 应用 (测试跨 Pod 连通性和延迟)
	// 服务器: Pod0.Server0 (10.0.0.1:9)
	// 客户端: Pod1.Server0 (10.1.0.1)
	// 用途: 验证基本的跨 Pod 通信和往返延迟
	UdpEchoServerHelper echoServer(9);
	ApplicationContainer serverApps = echoServer.Install(pod0.Get(0));
	serverApps.Start(Seconds(1.0));
	serverApps.Stop(Seconds(10.0));

	UdpEchoClientHelper echoClient(pod0_Iface.GetAddress(0), 9);
	echoClient.SetAttribute("MaxPackets", UintegerValue(1));      // 发送 1 个数据包
	echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0))); // 发送间隔 1 秒
	echoClient.SetAttribute("PacketSize", UintegerValue(1024));   // 数据包大小 1024 字节
	ApplicationContainer clientApps = echoClient.Install(pod1.Get(0));
	clientApps.Start(Seconds(2.0));
	clientApps.Stop(Seconds(10.0));

	// 7.2 TCP BulkSend 应用 (测试吞吐量和 ECMP 负载均衡)
	// Sink (接收端): Pod2.Server0 (10.2.0.1:80)
	// Source (发送端): Pod3.Server0 (10.3.0.1)
	// 用途: 生成大量 TCP 流量，测试网络吞吐量和 ECMP 效果
	uint16_t port = 80;
	
	// 创建 TCP 接收端 (Sink)
	PacketSinkHelper sink("ns3::TcpSocketFactory", 
	                      InetSocketAddress(Ipv4Address::GetAny(), port));
	ApplicationContainer sinkApp = sink.Install(pod2.Get(0));
	sinkApp.Start(Seconds(1.0));
	sinkApp.Stop(Seconds(10.0));
 
	// 创建 TCP 发送端 (BulkSend)
	BulkSendHelper source("ns3::TcpSocketFactory", 
	                      InetSocketAddress(pod2_Iface.GetAddress(0), port));
	source.SetAttribute("MaxBytes", UintegerValue(1000000)); // 发送 1 MB 数据
	ApplicationContainer sourceApps = source.Install(pod3.Get(0));
	sourceApps.Start(Seconds(1.5)); // 稍晚启动，避免与 UDP 冲突
	sourceApps.Stop(Seconds(10.0));
 
	// ========================================================================
	// 8. 配置监控与可视化
	// ========================================================================
	
	// 8.1 启用 PCAP 数据包捕获
	// 为所有服务器到交换机的链路生成 .pcap 文件
	// 可以使用 Wireshark 打开这些文件进行详细的数据包分析
	NodeToSW.EnablePcapAll("DCN_FatTree_CSMA_Pcap");
	
	// 8.2 安装 FlowMonitor (流量监控器)
	// FlowMonitor 会自动跟踪网络中的所有流 (Flow)
	// 收集的统计信息包括:
	//   - 发送/接收的数据包数量和字节数
	//   - 延迟 (Delay)
	//   - 抖动 (Jitter)
	//   - 丢包率 (Packet Loss Rate)
	FlowMonitorHelper flowmonHelper;
	flowmonHelper.InstallAll();
	
	// 8.3 配置 NetAnim 动画
	// 为每个节点设置固定的二维坐标，用于在 NetAnim 中可视化拓扑
	// NetAnim 可以播放仿真过程，显示数据包在网络中的传输路径
	AnimationInterface anim("animation.xml");
	
	// Pod 0 节点位置 (左侧)
	anim.SetConstantPosition(pod0.Get(0), 1.0, 20.0);  // Server 0
	anim.SetConstantPosition(pod0.Get(1), 2.0, 20.0);  // Server 1
	anim.SetConstantPosition(pod0.Get(2), 3.0, 20.0);  // Server 2
	anim.SetConstantPosition(pod0.Get(3), 4.0, 20.0);  // Server 3
	anim.SetConstantPosition(pod0.Get(4), 1.5, 16.0);  // Access SW 4
	anim.SetConstantPosition(pod0.Get(5), 3.5, 16.0);  // Access SW 5
	anim.SetConstantPosition(pod0.Get(6), 1.5, 12.0);  // Aggr SW 6
	anim.SetConstantPosition(pod0.Get(7), 3.5, 12.0);  // Aggr SW 7
	
	// Pod 1 节点位置 (中左)
	anim.SetConstantPosition(pod1.Get(0), 6.0, 20.0);
	anim.SetConstantPosition(pod1.Get(1), 7.0, 20.0);
	anim.SetConstantPosition(pod1.Get(2), 8.0, 20.0);
	anim.SetConstantPosition(pod1.Get(3), 9.0, 20.0);
	anim.SetConstantPosition(pod1.Get(4), 6.5, 16.0);
	anim.SetConstantPosition(pod1.Get(5), 8.5, 16.0);
	anim.SetConstantPosition(pod1.Get(6), 6.5, 12.0);
	anim.SetConstantPosition(pod1.Get(7), 8.5, 12.0);
	
	// Pod 2 节点位置 (中右)
	anim.SetConstantPosition(pod2.Get(0), 11.0, 20.0);
	anim.SetConstantPosition(pod2.Get(1), 12.0, 20.0);
	anim.SetConstantPosition(pod2.Get(2), 13.0, 20.0);
	anim.SetConstantPosition(pod2.Get(3), 14.0, 20.0);
	anim.SetConstantPosition(pod2.Get(4), 11.5, 16.0);
	anim.SetConstantPosition(pod2.Get(5), 13.5, 16.0);
	anim.SetConstantPosition(pod2.Get(6), 11.5, 12.0);
	anim.SetConstantPosition(pod2.Get(7), 13.5, 12.0);
	
	// Pod 3 节点位置 (右侧)
	anim.SetConstantPosition(pod3.Get(0), 16.0, 20.0);
	anim.SetConstantPosition(pod3.Get(1), 17.0, 20.0);
	anim.SetConstantPosition(pod3.Get(2), 18.0, 20.0);
	anim.SetConstantPosition(pod3.Get(3), 19.0, 20.0);
	anim.SetConstantPosition(pod3.Get(4), 16.5, 16.0);
	anim.SetConstantPosition(pod3.Get(5), 18.5, 16.0);
	anim.SetConstantPosition(pod3.Get(6), 16.5, 12.0);
	anim.SetConstantPosition(pod3.Get(7), 18.5, 12.0);
	
	// 核心层交换机位置 (底部中央，均匀分布)
	anim.SetConstantPosition(core.Get(0), 2.5, 7.0);   // Core 0
	anim.SetConstantPosition(core.Get(1), 7.5, 7.0);   // Core 1
	anim.SetConstantPosition(core.Get(2), 12.5, 7.0);  // Core 2
	anim.SetConstantPosition(core.Get(3), 17.5, 7.0);  // Core 3
			
	// ========================================================================
	// 9. 运行仿真与收尾
	// ========================================================================
	
	// 9.1 设置仿真停止时间
	// 设置为 11.0 秒，确保所有应用 (10.0 秒停止) 都能完成
	Simulator::Stop(Seconds(11.0));
	
	// 9.2 启动仿真
	NS_LOG_INFO("Starting simulation...");
	Simulator::Run();
	NS_LOG_INFO("Simulation completed.");
	
	// 9.3 导出 FlowMonitor 统计数据
	// 生成 DCN_FatTree_FlowStat.flowmon 文件，包含所有流的详细统计信息
	// 参数说明:
	//   - 第一个 true: 包含每个流的详细信息
	//   - 第二个 true: 包含每个探针 (Probe) 的详细信息
	flowmonHelper.SerializeToXmlFile("DCN_FatTree_FlowStat.flowmon", true, true);
	NS_LOG_INFO("FlowMonitor statistics exported to DCN_FatTree_FlowStat.flowmon");
	
	// 9.4 清理仿真器，释放所有分配的内存
	Simulator::Destroy();
	NS_LOG_INFO("Simulation resources cleaned up. Done.");

	return 0;
}