/*
 * ============================================================================
 * 标题: Fat-Tree 静态路由实现
 * ============================================================================
 *
 * 描述:
 *   本程序实现 Fat-Tree 拓扑的改进版方案:
 *   1. IP 地址分配: 每条链路使用独立的 /30 子网
 *   2. 静态路由聚合算法:
 *      - 使用 /24 和 /16 子网掩码进行路由聚合
 *      - 减少路由表条目，提高路由效率
 *   3. 使用静态路由表，不依赖全局 ECMP 路由
 *
 * 本实现地址分配规则 (改进版):
 *   - 服务器到交换机: 每条链路使用独立的 /30 子网
 *     格式: 10.pod.switch.(server*4)/30
 *     例如: Pod 0, Server 0 -> AccessSW 0: 10.0.0.0/30
 *   - 交换机间链路: 使用独立的 /30 子网避免冲突
 *
 * 静态路由聚合算法:
 *   - 接入交换机: 使用 /24 掩码聚合本交换机下服务器
 *   - 汇聚交换机: 使用 /24 掩码聚合接入层，使用 /16 掩码聚合其他 Pod
 *   - 核心交换机: 使用 /16 掩码为每个 Pod 添加路由
 *
 * 版本: 1.0
 * 作者: [Liu Mengxuan]
 * 日期: 2025-11-10
 * ns-3 版本: 3.44
 * ============================================================================
 */

// ============================================================================
// 头文件引入
// ============================================================================
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-helper.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("DCN_FatTree_Custom");

// ============================================================================
// 全局变量和常量
// ============================================================================

// Fat-Tree 参数
const int K = 4;  // Fat-Tree 的 k 值
const int NUM_PODS = K;
const int NUM_CORE = (K/2) * (K/2);
const int SERVERS_PER_POD = (K/2) * (K/2);
const int SWITCHES_PER_POD = K;

// 节点容器
NodeContainer pods[4];      // 每个 Pod: 4 服务器 + 4 交换机
NodeContainer coreNodes;    // 核心交换机

// 接口容器 (用于获取 IP 地址)
std::map<std::string, Ipv4InterfaceContainer> interfaces;

// ============================================================================
// 辅助函数声明
// ============================================================================

/**
 * @brief 生成 Fat-Tree 风格的 IP 地址字符串 (未使用，仅供参考)
 * @param pod Pod ID (0-3)
 * @param sw Switch ID (0-3)
 * @param id Host/Port ID (1-4)
 * @return IP 地址字符串 "10.pod.switch.id"
 */
std::string GetFatTreeIP(int pod, int sw, int id);


// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char *argv[])
{
	// ========================================================================
	// 1. 配置模拟参数
	// ========================================================================
	
	CommandLine cmd;
	cmd.Parse(argc, argv);
	
	Time::SetResolution(Time::NS);
	LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
	LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
	
	NS_LOG_INFO("Building Fat-Tree topology with custom routing...");
	
	// ========================================================================
	// 2. 定义链路助手
	// ========================================================================
	
	PointToPointHelper serverToSwitch;
	serverToSwitch.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
	serverToSwitch.SetChannelAttribute("Delay", StringValue("1us"));
	
	PointToPointHelper switchToSwitch;
	switchToSwitch.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
	switchToSwitch.SetChannelAttribute("Delay", StringValue("1us"));
	
	// ========================================================================
	// 3. 创建节点
	// ========================================================================
	
	NS_LOG_INFO("Creating nodes...");
	
	// 创建每个 Pod 的节点
	for (int i = 0; i < NUM_PODS; i++) {
		pods[i].Create(8);  // 4 服务器 + 4 交换机
	}
	
	// 创建核心交换机
	coreNodes.Create(NUM_CORE);
	
	// 设置移动性模型
	MobilityHelper mobility;
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	for (int i = 0; i < NUM_PODS; i++) {
		mobility.Install(pods[i]);
	}
	mobility.Install(coreNodes);
	
	// 安装协议栈
	InternetStackHelper stack;
	for (int i = 0; i < NUM_PODS; i++) {
		stack.Install(pods[i]);
	}
	stack.Install(coreNodes);
	
	NS_LOG_INFO("Nodes created: " << NUM_PODS << " pods, " << NUM_CORE << " core switches");
	
	// ========================================================================
	// 4. 构建拓扑并分配 IP 地址 (改进版 /30 子网方案)
	// ========================================================================
	
	NS_LOG_INFO("Building topology and assigning IP addresses...");
	
	Ipv4AddressHelper address;
	
	// 4.1 为每个 Pod 构建内部连接
	for (int pod = 0; pod < NUM_PODS; pod++) {
		NS_LOG_INFO("Configuring Pod " << pod << "...");
		
		// Pod 内节点编号:
		// 0-3: 服务器
		// 4-5: 接入交换机 (Lower layer)
		// 6-7: 汇聚交换机 (Upper layer)
		
		// 连接服务器到接入交换机
		// Server 0,1 -> Access Switch 0 (节点4)
		// Server 2,3 -> Access Switch 1 (节点5)
		// 
		// IP 分配策略: 每条链路使用独立的 /30 子网
		// 格式: 10.pod.switch.offset/30
		for (int server = 0; server < 4; server++) {
			int accessSwitch = (server < 2) ? 4 : 5;  // 前2个连SW0，后2个连SW1
			int switchId = accessSwitch - 4;  // 0 或 1
			
			NetDeviceContainer link = serverToSwitch.Install(
				pods[pod].Get(server), 
				pods[pod].Get(accessSwitch)
			);
			
			// 为每条链路分配独立的 /30 子网，避免地址冲突
			// 10.pod.switch.(server*4)/30
			int offset = server * 4;  // 0, 4, 8, 12
			std::string subnet = "10." + std::to_string(pod) + "." + 
			                     std::to_string(switchId) + "." + 
			                     std::to_string(offset);
			address.SetBase(subnet.c_str(), "255.255.255.252");  // /30 子网
			
			Ipv4InterfaceContainer iface = address.Assign(link);
			
			// 保存接口信息
			std::string key = "pod" + std::to_string(pod) + "_server" + 
			                  std::to_string(server);
			interfaces[key] = iface;
			
			NS_LOG_INFO("  Server " << server << " <-> AccessSW " << switchId 
			           << ": " << iface.GetAddress(0) << " <-> " << iface.GetAddress(1));
		}
		
		// 连接接入交换机到汇聚交换机 (全连接)
		// 使用 /30 子网避免地址冲突
		for (int lower = 4; lower < 6; lower++) {      // 接入交换机 0,1
			for (int upper = 6; upper < 8; upper++) {  // 汇聚交换机 0,1
				NetDeviceContainer link = switchToSwitch.Install(
					pods[pod].Get(lower),
					pods[pod].Get(upper)
				);
				
				// 为交换机间链路分配独立的 /30 子网
				int lowerId = lower - 4;
				int upperId = upper - 6;
				int linkId = lowerId * 2 + upperId;  // 0, 1, 2, 3
				int offset = 16 + linkId * 4;  // 16, 20, 24, 28 (避免与服务器链路冲突)
				std::string subnet = "10." + std::to_string(pod) + "." + 
				                     std::to_string(2 + upperId) + "." + 
				                     std::to_string(offset);
				address.SetBase(subnet.c_str(), "255.255.255.252");  // /30 子网
				
				Ipv4InterfaceContainer iface = address.Assign(link);
				
				std::string key = "pod" + std::to_string(pod) + "_lower" + 
				                  std::to_string(lowerId) + "_upper" + std::to_string(upperId);
				interfaces[key] = iface;
			}
		}
	}
	
	// 4.2 连接汇聚交换机到核心交换机
	NS_LOG_INFO("Connecting aggregation switches to core switches...");
	
	int coreLink = 0;  // 核心链路计数器
	for (int i = 0; i < K/2; i++) {      // 核心交换机组
		for (int j = 0; j < K/2; j++) {  // 每组内的交换机
			int coreId = i * (K/2) + j;
			
			// 连接到每个 Pod 的对应汇聚交换机
			for (int pod = 0; pod < NUM_PODS; pod++) {
				int aggrSwitch = 6 + i;  // 汇聚交换机 6 或 7
				
				NetDeviceContainer link = switchToSwitch.Install(
					pods[pod].Get(aggrSwitch),
					coreNodes.Get(coreId)
				);
				
				// 为核心链路分配独立的 /30 子网
				// 使用 10.10.x.y 格式，其中 x 和 y 根据 coreLink 计算
				int subnet3 = coreLink / 64;  // 第三个八位组
				int subnet4 = (coreLink % 64) * 4;  // 第四个八位组 (每个 /30 占用 4 个地址)
				std::string subnet = "10.10." + std::to_string(subnet3) + "." + 
				                     std::to_string(subnet4);
				address.SetBase(subnet.c_str(), "255.255.255.252");  // /30 子网
				
				Ipv4InterfaceContainer iface = address.Assign(link);
				
				std::string key = "pod" + std::to_string(pod) + "_aggr" + 
				                  std::to_string(i) + "_core" + std::to_string(coreId);
				interfaces[key] = iface;
				
				NS_LOG_INFO("  Pod" << pod << ".AggrSW" << i << " <-> Core" << coreId 
				           << ": " << subnet << "/30");
				
				coreLink++;
			}
		}
	}
	
	// ========================================================================
	// 5. 配置自定义路由 (静态路由聚合算法)
	// ========================================================================
	
	NS_LOG_INFO("Configuring custom routing tables...");
	
	// 5.1 配置服务器路由 (默认网关指向接入交换机)
	for (int pod = 0; pod < NUM_PODS; pod++) {
		for (int server = 0; server < 4; server++) {
			// 获取服务器连接的接入交换机的 IP 地址
			std::string key = "pod" + std::to_string(pod) + "_server" + 
			                  std::to_string(server);
			Ipv4Address gatewayIP = interfaces[key].GetAddress(1);  // 交换机端口
			
			Ptr<Ipv4> ipv4 = pods[pod].Get(server)->GetObject<Ipv4>();
			Ptr<Ipv4StaticRouting> staticRouting = Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(
				ipv4->GetRoutingProtocol()
			);
			staticRouting->SetDefaultRoute(gatewayIP, 1);
			
			NS_LOG_DEBUG("Server " << server << " in Pod " << pod 
			            << " default route via " << gatewayIP);
		}
	}
	
	// 5.2 配置接入交换机路由
	// 接入交换机: 为本 Pod 内其他接入交换机下的服务器添加路由，为跨 Pod 流量添加上行路由
	for (int pod = 0; pod < NUM_PODS; pod++) {
		for (int accessId = 0; accessId < 2; accessId++) {
			Ptr<Ipv4> ipv4 = pods[pod].Get(4 + accessId)->GetObject<Ipv4>();
			Ptr<Ipv4StaticRouting> staticRouting = Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(
				ipv4->GetRoutingProtocol()
			);
			
			// 获取到汇聚交换机的下一跳地址
			std::string key = "pod" + std::to_string(pod) + "_lower" + 
			                  std::to_string(accessId) + "_upper0";
			Ipv4Address nextHop = interfaces[key].GetAddress(1);  // 汇聚交换机端
			int outInterface = ipv4->GetInterfaceForAddress(interfaces[key].GetAddress(0));
			
			// 1. 为本 Pod 内其他接入交换机下的服务器添加网络路由（聚合路由）
			//    例如: AccessSW 0 需要为 AccessSW 1 下的服务器添加网络路由
			int otherAccessId = (accessId == 0) ? 1 : 0;  // 另一个接入交换机

			// 计算另一个接入交换机下服务器的子网
			// AccessSW 0: 10.pod.0.0/24 (覆盖 10.pod.0.0/30 和 10.pod.0.4/30)
			// AccessSW 1: 10.pod.1.0/24 (覆盖 10.pod.1.8/30 和 10.pod.1.12/30)
			int switchId = otherAccessId;
			std::string otherSubnet = "10." + std::to_string(pod) + "." +
			                         std::to_string(switchId) + ".0";

			// 添加网络路由，使用 /24 子网掩码聚合整个接入交换机下的服务器
			staticRouting->AddNetworkRouteTo(
				Ipv4Address(otherSubnet.c_str()),
				Ipv4Mask("255.255.255.0"),  // /24 子网掩码，覆盖整个第三八位组
				nextHop,
				outInterface
			);

			NS_LOG_DEBUG("Access switch " << accessId << " in Pod " << pod
			            << " -> subnet " << otherSubnet << "/24 via " << nextHop);
			
			// 2. 为其他 Pod 添加网络路由（通过汇聚交换机）
			for (int otherPod = 0; otherPod < NUM_PODS; otherPod++) {
				if (otherPod != pod) {
					std::string subnet = "10." + std::to_string(otherPod) + ".0.0";
					staticRouting->AddNetworkRouteTo(
						Ipv4Address(subnet.c_str()),
						Ipv4Mask("255.255.0.0"),
						nextHop,
						outInterface
					);
				}
			}
			
			NS_LOG_DEBUG("Access switch " << accessId << " in Pod " << pod 
			            << " configured with uplink next-hop " << nextHop);
		}
	}
	
	// 5.3 配置汇聚交换机路由
	// 汇聚交换机: 为本 Pod 内流量添加下行路由，为跨 Pod 流量添加上行路由到核心层
	for (int pod = 0; pod < NUM_PODS; pod++) {
		for (int aggrId = 0; aggrId < 2; aggrId++) {
			Ptr<Ipv4> ipv4 = pods[pod].Get(6 + aggrId)->GetObject<Ipv4>();
			Ptr<Ipv4StaticRouting> staticRouting = Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(
				ipv4->GetRoutingProtocol()
			);
			
			// 为本 Pod 内的服务器添加下行网络路由（聚合路由）
			// 每个汇聚交换机连接到两个接入交换机，为每个接入交换机下的服务器子网添加路由
			for (int accessId = 0; accessId < 2; accessId++) {
				std::string key = "pod" + std::to_string(pod) + "_lower" +
				                  std::to_string(accessId) + "_upper" + std::to_string(aggrId);
				Ipv4Address nextHop = interfaces[key].GetAddress(0);  // 接入交换机端
				int outInterface = ipv4->GetInterfaceForAddress(interfaces[key].GetAddress(1));

				// 为该接入交换机下的服务器子网添加网络路由
				// AccessSW 0: 10.pod.0.0/24 (覆盖所有连接到AccessSW 0的服务器)
				// AccessSW 1: 10.pod.1.0/24 (覆盖所有连接到AccessSW 1的服务器)
				std::string serverSubnet = "10." + std::to_string(pod) + "." +
				                          std::to_string(accessId) + ".0";

				staticRouting->AddNetworkRouteTo(
					Ipv4Address(serverSubnet.c_str()),
					Ipv4Mask("255.255.255.0"),  // /24 子网掩码，聚合整个接入交换机
					nextHop,
					outInterface
				);

				NS_LOG_DEBUG("Aggregation switch " << aggrId << " in Pod " << pod
				            << " -> subnet " << serverSubnet << "/24 via " << nextHop);
			}
			
			// 为其他 Pod 添加上行路由到核心层
			// 每个汇聚交换机 aggrId 连接到对应组的核心交换机
			// aggrId=0 连接 core0,1; aggrId=1 连接 core2,3
			for (int j = 0; j < K/2; j++) {
				int coreId = aggrId * (K/2) + j;  // 正确计算核心交换机ID
				std::string coreKey = "pod" + std::to_string(pod) + "_aggr" + 
				                      std::to_string(aggrId) + "_core" + std::to_string(coreId);
				Ipv4Address coreNextHop = interfaces[coreKey].GetAddress(1);  // 核心交换机端
				
				// 为其他 Pod 添加路由（通过这个核心交换机）
				for (int otherPod = 0; otherPod < NUM_PODS; otherPod++) {
					if (otherPod != pod) {
						std::string subnet = "10." + std::to_string(otherPod) + ".0.0";
						staticRouting->AddNetworkRouteTo(
							Ipv4Address(subnet.c_str()),
							Ipv4Mask("255.255.0.0"),
							coreNextHop,
							ipv4->GetInterfaceForAddress(interfaces[coreKey].GetAddress(0))
						);
					}
				}
				
				NS_LOG_DEBUG("Aggregation switch " << aggrId << " in Pod " << pod 
				            << " -> Core " << coreId << " via " << coreNextHop);
			}
		}
	}
	
	// 5.4 配置核心交换机路由
	// 核心交换机: 为每个 Pod 添加下行路由
	for (int i = 0; i < K/2; i++) {
		for (int j = 0; j < K/2; j++) {
			int coreId = i * (K/2) + j;
			Ptr<Ipv4> ipv4 = coreNodes.Get(coreId)->GetObject<Ipv4>();
			Ptr<Ipv4StaticRouting> staticRouting = Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(
				ipv4->GetRoutingProtocol()
			);
			
			// 为每个 Pod 添加路由，通过对应的汇聚交换机
			for (int pod = 0; pod < NUM_PODS; pod++) {
				std::string key = "pod" + std::to_string(pod) + "_aggr" + 
				                  std::to_string(i) + "_core" + std::to_string(coreId);
				Ipv4Address nextHop = interfaces[key].GetAddress(0);  // 汇聚交换机端
				
				std::string subnet = "10." + std::to_string(pod) + ".0.0";
				staticRouting->AddNetworkRouteTo(
					Ipv4Address(subnet.c_str()),
					Ipv4Mask("255.255.0.0"),
					nextHop,
					ipv4->GetInterfaceForAddress(interfaces[key].GetAddress(1))
				);
				
				NS_LOG_DEBUG("Core switch " << coreId << " -> Pod " << pod 
				            << " via " << nextHop);
			}
		}
	}
	
	NS_LOG_INFO("Custom routing tables configured.");
	
	// ========================================================================
	// 6. 部署应用程序
	// ========================================================================
	
	NS_LOG_INFO("Deploying applications...");
	
	// ========================================================================
	// 测试 1: 跨 Pod 通信 (Pod1.Server0 -> Pod0.Server0)
	// ========================================================================
	NS_LOG_INFO("Setting up Test 1: Cross-Pod communication");
	
	UdpEchoServerHelper echoServer1(9);
	ApplicationContainer serverApps1 = echoServer1.Install(pods[0].Get(0));
	serverApps1.Start(Seconds(1.0));
	serverApps1.Stop(Seconds(10.0));
	
	std::string key1 = "pod0_server0";
	Ipv4Address serverAddr1 = interfaces[key1].GetAddress(0);
	
	UdpEchoClientHelper echoClient1(serverAddr1, 9);
	echoClient1.SetAttribute("MaxPackets", UintegerValue(10));
	echoClient1.SetAttribute("Interval", TimeValue(Seconds(1.0)));
	echoClient1.SetAttribute("PacketSize", UintegerValue(1024));
	ApplicationContainer clientApps1 = echoClient1.Install(pods[1].Get(0));
	clientApps1.Start(Seconds(2.0));
	clientApps1.Stop(Seconds(10.0));
	
	NS_LOG_INFO("Test 1: Pod1.Server0 -> Pod0.Server0 (" << serverAddr1 << ")");
	
	// ========================================================================
	// 测试 2: 同 Pod 内跨接入交换机通信 (Pod0.Server0 -> Pod0.Server2)
	// Server 0 连接到 AccessSW 0
	// Server 2 连接到 AccessSW 1
	// ========================================================================
	NS_LOG_INFO("Setting up Test 2: Intra-Pod cross-access-switch communication");
	
	UdpEchoServerHelper echoServer2(10);
	ApplicationContainer serverApps2 = echoServer2.Install(pods[0].Get(2));  // Server 2
	serverApps2.Start(Seconds(1.0));
	serverApps2.Stop(Seconds(10.0));
	
	std::string key2 = "pod0_server2";
	Ipv4Address serverAddr2 = interfaces[key2].GetAddress(0);
	
	UdpEchoClientHelper echoClient2(serverAddr2, 10);
	echoClient2.SetAttribute("MaxPackets", UintegerValue(10));
	echoClient2.SetAttribute("Interval", TimeValue(Seconds(1.0)));
	echoClient2.SetAttribute("PacketSize", UintegerValue(1024));
	ApplicationContainer clientApps2 = echoClient2.Install(pods[0].Get(0));  // Server 0
	clientApps2.Start(Seconds(2.0));
	clientApps2.Stop(Seconds(10.0));
	
	NS_LOG_INFO("Test 2: Pod0.Server0 -> Pod0.Server2 (" << serverAddr2 << ")");
	
	// ========================================================================
	// 7. 配置监控
	// ========================================================================
	
	FlowMonitorHelper flowmonHelper;
	flowmonHelper.InstallAll();
	
	AnimationInterface anim("animation_custom.xml");
	
	// 设置节点位置
	for (int pod = 0; pod < NUM_PODS; pod++) {
		double xBase = pod * 5.0;
		for (int i = 0; i < 4; i++) {
			anim.SetConstantPosition(pods[pod].Get(i), xBase + i, 20.0);  // 服务器
		}
		anim.SetConstantPosition(pods[pod].Get(4), xBase + 0.5, 16.0);  // Access SW 0
		anim.SetConstantPosition(pods[pod].Get(5), xBase + 2.5, 16.0);  // Access SW 1
		anim.SetConstantPosition(pods[pod].Get(6), xBase + 0.5, 12.0);  // Aggr SW 0
		anim.SetConstantPosition(pods[pod].Get(7), xBase + 2.5, 12.0);  // Aggr SW 1
	}
	
	for (int i = 0; i < NUM_CORE; i++) {
		anim.SetConstantPosition(coreNodes.Get(i), i * 5.0 + 1.5, 7.0);
	}
	
	// ========================================================================
	// 8. 运行仿真
	// ========================================================================
	
	NS_LOG_INFO("Starting simulation...");
	
	Simulator::Stop(Seconds(11.0));
	Simulator::Run();
	
	flowmonHelper.SerializeToXmlFile("DCN_FatTree_Custom_FlowStat.flowmon", true, true);
	
	Simulator::Destroy();
	
	NS_LOG_INFO("Simulation completed.");
	
	return 0;
}

// ============================================================================
// 辅助函数实现
// ============================================================================

std::string GetFatTreeIP(int pod, int sw, int id)
{
	return "10." + std::to_string(pod) + "." + 
	       std::to_string(sw) + "." + std::to_string(id);
}

