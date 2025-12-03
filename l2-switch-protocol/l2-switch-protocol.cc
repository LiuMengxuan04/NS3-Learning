/*
 * ============================================================================
 * 标题: 使用协议栈实现的 L2 交换机模拟 - 多交换机拓扑
 * ============================================================================
 *
 * 【程序目的】
 *   使用 ns-3 标准的协议栈（Protocol Stack）架构实现多个 L2 交换机的网络。
 *   1. 创建自定义 L2 协议类（L2SwitchProtocol），继承 Object
 *   2. 使用 Helper 类来安装协议到节点
 *   3. 通过 node->AggregateObject() 将协议附加到节点
 *   4. 主机端使用标准的 InternetStackHelper 安装完整协议栈
 *
 * 【网络拓扑】
 *
 *      Host A                  Host B                  Host C
 *   (192.168.1.1)          (192.168.1.2)           (192.168.1.3)
 *         |                      |                       |
 *         |                      |                       |
 *   +-----+-----+          +-----+-----+           +-----+-----+
 *   | Switch 0  |          | Switch 1  |           | Switch 2  |
 *   +-----+-----+          +-----+-----+           +-----+-----+
 *     端口0  端口1         端口0 端口1 端口2        端口0  端口1
 *       |      |             |     |     |            |      |
 *       |      +-------------+     |     +------------+      |
 *       |        (SW0-SW1)         |       (SW1-SW2)         |
 *    Host A                     Host B                    Host C
 *
 *   简化视图:
 *
 *       [Host A]              [Host B]              [Host C]
 *          │                     │                     │
 *          │                     │                     │
 *     ┌────┴────┐           ┌────┴────┐           ┌────┴────┐
 *     │ Switch0 │───────────│ Switch1 │───────────│ Switch2 │
 *     └─────────┘           └─────────┘           └─────────┘
 *
 * 【核心概念】
 *
 * 1. 什么是协议栈（Protocol Stack）？
 *    - 在 ns-3 中，协议是通过聚合（Aggregate）到节点的 Object 实现的
 *    - 例如：InternetStackHelper 会将 Ipv4L3Protocol、ArpL3Protocol 等聚合到节点
 *    - 协议对象可以通过 node->GetObject<ProtocolType>() 获取
 *
 * 2. 协议如何工作？
 *    - 协议对象在节点初始化时聚合到节点
 *    - 协议注册回调函数监听设备上的数据包
 *    - 当数据包到达时，回调函数被触发，协议处理数据包
 *
 * 3. 多交换机转发
 *    - 每个交换机独立学习 MAC 地址
 *    - 数据包通过交换机链路逐跳转发
 *    - 首次通信时泛洪，学习后单播转发
 *
 * 作者: Liu Mengxuan
 * ns-3 版本: 3.44
 * ============================================================================
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include <map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("L2SwitchProtocol");

// ============================================================================
// 【第一部分】自定义 L2 交换协议 (L2SwitchProtocol)
// ============================================================================
//
// 【核心设计】
//   这是一个协议对象，继承自 Object（ns-3 的基类）
//   它被聚合到交换机节点上，负责：
//   1. 监听节点上所有网络设备的数据包
//   2. 学习 MAC 地址与端口的映射关系
//   3. 根据目的 MAC 地址转发数据包
//
// ============================================================================

class L2SwitchProtocol : public Object
{
public:
    // ========== 类型系统 ==========
    static TypeId GetTypeId();

    L2SwitchProtocol();
    ~L2SwitchProtocol() override;

    // ========== 协议初始化 ==========

    /**
     * @brief 设置协议所属的节点
     * @param node 节点指针
     */
    void SetNode(Ptr<Node> node);

    /**
     * @brief 获取协议所属的节点
     * @return 节点指针
     */
    Ptr<Node> GetNode() const;

    /**
     * @brief 初始化协议（在节点上的所有设备都添加完毕后调用）
     *
     * 这个方法会：
     * 1. 遍历节点上的所有网络设备
     * 2. 为每个设备注册混杂模式回调
     * 3. 开始监听所有数据包
     */
    void Initialize();

    /**
     * @brief 设置交换机名称（用于日志）
     * @param name 交换机名称
     */
    void SetSwitchName(const std::string& name);

protected:
    void DoDispose() override;
    void DoInitialize() override;

private:
    // ========== 核心转发逻辑 ==========

    /**
     * @brief 接收到数据包的回调函数
     *
     * 这是交换机的核心逻辑：
     * 1. 学习源 MAC 地址
     * 2. 查找目的 MAC 地址
     * 3. 转发或泛洪
     */
    bool ReceiveFromDevice(Ptr<NetDevice> inDevice,
                          Ptr<const Packet> packet,
                          uint16_t protocol,
                          const Address& from,
                          const Address& to,
                          NetDevice::PacketType packetType);

    /**
     * @brief MAC 地址学习
     * @param source 源 MAC 地址
     * @param inDevice 入端口设备
     */
    void Learn(Mac48Address source, Ptr<NetDevice> inDevice);

    /**
     * @brief 查找学习到的端口
     * @param destination 目的 MAC 地址
     * @return 端口设备，如果未找到返回 nullptr
     */
    Ptr<NetDevice> GetLearnedPort(Mac48Address destination);

    /**
     * @brief 单播转发
     * @param outDevice 出端口
     * @param packet 数据包
     * @param protocol 协议类型
     * @param destination 目的地址
     */
    void ForwardUnicast(Ptr<NetDevice> outDevice,
                       Ptr<const Packet> packet,
                       uint16_t protocol,
                       const Mac48Address& source,
                       const Mac48Address& destination);

    /**
     * @brief 广播转发（泛洪）
     * @param inDevice 入端口（不向它回发）
     * @param packet 数据包
     * @param protocol 协议类型
     * @param destination 目的地址
     */
    void ForwardBroadcast(Ptr<NetDevice> inDevice,
                         Ptr<const Packet> packet,
                         uint16_t protocol,
                         const Mac48Address& source,
                         const Mac48Address& destination);

    // ========== 成员变量 ==========

    std::string m_switchName;                           // 交换机名称
    Ptr<Node> m_node;                                   // 所属节点
    std::map<Mac48Address, Ptr<NetDevice>> m_macTable;  // MAC 地址学习表
    bool m_initialized;                                 // 是否已初始化
};

// ========== 实现 TypeId ==========
TypeId
L2SwitchProtocol::GetTypeId()
{
    static TypeId tid = TypeId("ns3::L2SwitchProtocol")
        .SetParent<Object>()
        .SetGroupName("Network")
        .AddConstructor<L2SwitchProtocol>();
    return tid;
}

// ========== 构造和析构 ==========
L2SwitchProtocol::L2SwitchProtocol()
    : m_switchName("Switch"),
      m_node(nullptr),
      m_initialized(false)
{
    NS_LOG_FUNCTION(this);
}

L2SwitchProtocol::~L2SwitchProtocol()
{
    NS_LOG_FUNCTION(this);
}

void
L2SwitchProtocol::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_node = nullptr;
    m_macTable.clear();
    Object::DoDispose();
}

void
L2SwitchProtocol::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    Object::DoInitialize();
}

// ========== Getter/Setter ==========
void
L2SwitchProtocol::SetNode(Ptr<Node> node)
{
    NS_LOG_FUNCTION(this << node);
    m_node = node;
}

Ptr<Node>
L2SwitchProtocol::GetNode() const
{
    return m_node;
}

void
L2SwitchProtocol::SetSwitchName(const std::string& name)
{
    m_switchName = name;
}

// ========== 初始化协议 ==========
void
L2SwitchProtocol::Initialize()
{
    NS_LOG_FUNCTION(this);

    if (m_initialized)
    {
        return;
    }

    NS_ASSERT_MSG(m_node != nullptr, "Node must be set before Initialize()");

    // 遍历节点上的所有网络设备
    uint32_t nDevices = m_node->GetNDevices();
    NS_LOG_INFO(m_switchName << ": Initializing with " << nDevices << " devices");

    for (uint32_t i = 0; i < nDevices; ++i)
    {
        Ptr<NetDevice> device = m_node->GetDevice(i);

        // 注册混杂模式回调
        // 这样我们就能监听所有经过这个设备的数据包
        device->SetPromiscReceiveCallback(
            MakeCallback(&L2SwitchProtocol::ReceiveFromDevice, this));

        NS_LOG_INFO(m_switchName << ": Registered callback on device " << i
                   << " (MAC: " << Mac48Address::ConvertFrom(device->GetAddress()) << ")");
    }

    m_initialized = true;
}

// ========== 核心转发逻辑 ==========
bool
L2SwitchProtocol::ReceiveFromDevice(Ptr<NetDevice> inDevice,
                                   Ptr<const Packet> packet,
                                   uint16_t protocol,
                                   const Address& from,
                                   const Address& to,
                                   NetDevice::PacketType packetType)
{
    NS_LOG_FUNCTION(this << inDevice << packet << protocol << from << to << packetType);

    // 将地址转换为 MAC 地址（真实的数据帧来源和目的）
    Mac48Address srcMac = Mac48Address::ConvertFrom(from);
    Mac48Address dstMac = Mac48Address::ConvertFrom(to);

    NS_LOG_DEBUG(m_switchName << ": Received packet from " << srcMac << " to " << dstMac
                << " on device " << inDevice->GetIfIndex());

    // 步骤 1: 学习源 MAC 地址
    Learn(srcMac, inDevice);

    // 步骤 2: 转发决策
    if (dstMac.IsBroadcast())
    {
        // 广播帧 - 泛洪到所有端口
        NS_LOG_INFO(m_switchName << ": Broadcasting packet from " << srcMac);
        ForwardBroadcast(inDevice, packet, protocol, srcMac, dstMac);
    }
    else
    {
        // 单播帧 - 查找目的端口
        Ptr<NetDevice> outDevice = GetLearnedPort(dstMac);

        if (outDevice && outDevice != inDevice)
        {
            // 已知目的端口，单播转发
            NS_LOG_INFO(m_switchName << ": Forwarding " << srcMac << " -> " << dstMac
                       << " via port " << outDevice->GetIfIndex());
            ForwardUnicast(outDevice, packet, protocol, srcMac, dstMac);
        }
        else if (!outDevice)
        {
            // 未知目的端口，泛洪
            NS_LOG_INFO(m_switchName << ": Unknown destination " << dstMac << ", flooding");
            ForwardBroadcast(inDevice, packet, protocol, srcMac, dstMac);
        }
        else
        {
            // 目的端口就是入端口，丢弃（避免环路）
            NS_LOG_DEBUG(m_switchName << ": Dropping packet, destination on same port");
        }
    }

    return true;  // 返回 true 表示数据包已处理
}

// ========== MAC 地址学习 ==========
void
L2SwitchProtocol::Learn(Mac48Address source, Ptr<NetDevice> inDevice)
{
    auto it = m_macTable.find(source);

    if (it == m_macTable.end())
    {
        // 新 MAC 地址
        m_macTable[source] = inDevice;
        NS_LOG_INFO(m_switchName << ": Learned " << source
                   << " on port " << inDevice->GetIfIndex());
    }
    else if (it->second != inDevice)
    {
        // MAC 地址对应的端口变了
        it->second = inDevice;
        NS_LOG_INFO(m_switchName << ": Updated " << source
                   << " to port " << inDevice->GetIfIndex());
    }
}

// ========== 查找学习到的端口 ==========

Ptr<NetDevice>
L2SwitchProtocol::GetLearnedPort(Mac48Address destination)
{
    auto it = m_macTable.find(destination);
    return (it != m_macTable.end()) ? it->second : nullptr;
}

// ========== 单播转发 ==========
void
L2SwitchProtocol::ForwardUnicast(Ptr<NetDevice> outDevice,
                                Ptr<const Packet> packet,
                                uint16_t protocol,
                                const Mac48Address& source,
                                const Mac48Address& destination)
{
    NS_LOG_FUNCTION(this << outDevice << packet << protocol << destination);

    // 保留原始源 MAC，使用 SendFrom 发送
    outDevice->SendFrom(packet->Copy(), source, destination, protocol);
}

// ========== 广播转发 ==========
void
L2SwitchProtocol::ForwardBroadcast(Ptr<NetDevice> inDevice,
                                  Ptr<const Packet> packet,
                                  uint16_t protocol,
                                  const Mac48Address& source,
                                  const Mac48Address& destination)
{
    NS_LOG_FUNCTION(this << inDevice << packet << protocol << destination);

    // 向所有端口转发（除了入端口）
    uint32_t nDevices = m_node->GetNDevices();
    for (uint32_t i = 0; i < nDevices; ++i)
    {
        Ptr<NetDevice> device = m_node->GetDevice(i);
        if (device != inDevice)
        {
            device->SendFrom(packet->Copy(), source, destination, protocol);
        }
    }
}

// ============================================================================
// 【第二部分】L2 交换协议 Helper (L2SwitchHelper)
// ============================================================================
//
// 【作用】
//   Helper 类用于简化协议的安装和配置
//   类似于 InternetStackHelper，它负责：
//   1. 创建协议对象
//   2. 将协议聚合到节点
//   3. 初始化协议
//
// ============================================================================

class L2SwitchHelper
{
public:
    L2SwitchHelper();

    /**
     * @brief 在节点上安装 L2 交换协议
     * @param node 节点指针
     * @param name 交换机名称（用于日志）
     */
    void Install(Ptr<Node> node, const std::string& name = "Switch");

    /**
     * @brief 在节点容器上安装 L2 交换协议
     * @param nodes 节点容器
     */
    void Install(NodeContainer nodes);
};

// ========== 实现 L2SwitchHelper ==========
L2SwitchHelper::L2SwitchHelper()
{
}

void
L2SwitchHelper::Install(Ptr<Node> node, const std::string& name)
{
    NS_LOG_FUNCTION(node << name);

    // 检查是否已经安装过
    Ptr<L2SwitchProtocol> protocol = node->GetObject<L2SwitchProtocol>();
    if (protocol)
    {
        NS_LOG_WARN("L2SwitchProtocol already installed on node " << node->GetId());
        return;
    }

    // 创建协议对象
    protocol = CreateObject<L2SwitchProtocol>();
    protocol->SetSwitchName(name);
    protocol->SetNode(node);

    // 聚合到节点（这是关键步骤！）
    node->AggregateObject(protocol);

    NS_LOG_INFO("Installed L2SwitchProtocol on node " << node->GetId() << " (" << name << ")");
}

void
L2SwitchHelper::Install(NodeContainer nodes)
{
    uint32_t index = 0;
    for (auto i = nodes.Begin(); i != nodes.End(); ++i)
    {
        std::string name = "Switch" + std::to_string(index++);
        Install(*i, name);
    }
}

// ============================================================================
// 【第三部分】主函数 - 多交换机拓扑
// ============================================================================
//
// 【重要说明】
//   本实现完全使用自定义的 L2SwitchProtocol 来实现二层交换功能，
//   不依赖 ns-3 内置的 BridgeNetDevice。
//
//   网络拓扑:
//   Host A -- Switch0 -- Switch1 -- Switch2 -- Host C
//                          |
//                       Host B
//
//   关键技术点：
//   1. 使用 CSMA 链路保持 MAC 地址不变
//   2. 每个交换机独立运行 L2SwitchProtocol
//   3. 数据包通过多跳转发到达目的地
//
// ============================================================================

int main(int argc, char* argv[])
{
    // ========== 步骤 1: 启用日志 ==========
    LogComponentEnable("L2SwitchProtocol", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    // ========== 步骤 2: 创建节点 ==========
    NS_LOG_INFO("=== Creating Multi-Switch Network Topology ===");

    // 创建 3 个主机节点
    NodeContainer hosts;
    hosts.Create(3);  // Host A, Host B, Host C

    // 创建 3 个交换机节点
    NodeContainer switches;
    switches.Create(3);  // Switch 0, Switch 1, Switch 2

    NS_LOG_INFO("Topology: [Host A]-SW0-SW1-SW2-[Host C], [Host B]-SW1");

    // ========== 步骤 3: 创建 CSMA 链路 ==========
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));

    // 用于存储主机设备
    NetDeviceContainer hostDevices;

    // ----- 链路 1: Host A <-> Switch 0 -----
    {
        NodeContainer link;
        link.Add(hosts.Get(0));      // Host A
        link.Add(switches.Get(0));   // Switch 0
        NetDeviceContainer devices = csma.Install(link);
        hostDevices.Add(devices.Get(0));  // Host A 的设备
        NS_LOG_INFO("Created link: Host A <-> Switch0");
    }

    // ----- 链路 2: Switch 0 <-> Switch 1 -----
    {
        NodeContainer link;
        link.Add(switches.Get(0));   // Switch 0
        link.Add(switches.Get(1));   // Switch 1
        csma.Install(link);
        NS_LOG_INFO("Created link: Switch0 <-> Switch1");
    }

    // ----- 链路 3: Host B <-> Switch 1 -----
    {
        NodeContainer link;
        link.Add(hosts.Get(1));      // Host B
        link.Add(switches.Get(1));   // Switch 1
        NetDeviceContainer devices = csma.Install(link);
        hostDevices.Add(devices.Get(0));  // Host B 的设备
        NS_LOG_INFO("Created link: Host B <-> Switch1");
    }

    // ----- 链路 4: Switch 1 <-> Switch 2 -----
    {
        NodeContainer link;
        link.Add(switches.Get(1));   // Switch 1
        link.Add(switches.Get(2));   // Switch 2
        csma.Install(link);
        NS_LOG_INFO("Created link: Switch1 <-> Switch2");
    }

    // ----- 链路 5: Host C <-> Switch 2 -----
    {
        NodeContainer link;
        link.Add(hosts.Get(2));      // Host C
        link.Add(switches.Get(2));   // Switch 2
        NetDeviceContainer devices = csma.Install(link);
        hostDevices.Add(devices.Get(0));  // Host C 的设备
        NS_LOG_INFO("Created link: Host C <-> Switch2");
    }

    // ========== 步骤 4: 在所有交换机上安装 L2SwitchProtocol ==========
    L2SwitchHelper switchHelper;

    // 安装协议到每个交换机
    switchHelper.Install(switches.Get(0), "Switch0");
    switchHelper.Install(switches.Get(1), "Switch1");
    switchHelper.Install(switches.Get(2), "Switch2");

    // 初始化每个交换机的协议
    for (uint32_t i = 0; i < switches.GetN(); ++i)
    {
        Ptr<L2SwitchProtocol> protocol = switches.Get(i)->GetObject<L2SwitchProtocol>();
        NS_ASSERT(protocol);
        protocol->Initialize();
    }

    // ========== 步骤 5: 在主机节点上安装标准协议栈 ==========
    InternetStackHelper stack;
    stack.Install(hosts);

    // ========== 步骤 6: 分配 IP 地址 ==========
    Ipv4AddressHelper address;
    address.SetBase("192.168.1.0", "255.255.255.0");

    Ipv4InterfaceContainer hostInterfaces = address.Assign(hostDevices);

    NS_LOG_INFO("=== IP Addresses ===");
    NS_LOG_INFO("  Host A: " << hostInterfaces.GetAddress(0));
    NS_LOG_INFO("  Host B: " << hostInterfaces.GetAddress(1));
    NS_LOG_INFO("  Host C: " << hostInterfaces.GetAddress(2));

    // ========== 步骤 7: 安装应用程序 ==========
    // 在 Host C 上安装 UDP Echo Server
    UdpEchoServerHelper echoServer(9);
    ApplicationContainer serverApps = echoServer.Install(hosts.Get(2));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(10.0));

    // 在 Host A 上安装 UDP Echo Client (发送到 Host C)
    // 数据包路径: Host A -> Switch0 -> Switch1 -> Switch2 -> Host C
    UdpEchoClientHelper echoClientA(hostInterfaces.GetAddress(2), 9);
    echoClientA.SetAttribute("MaxPackets", UintegerValue(3));
    echoClientA.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClientA.SetAttribute("PacketSize", UintegerValue(512));

    ApplicationContainer clientAppsA = echoClientA.Install(hosts.Get(0));
    clientAppsA.Start(Seconds(1.0));
    clientAppsA.Stop(Seconds(10.0));

    // 在 Host B 上安装 UDP Echo Client (发送到 Host C)
    // 数据包路径: Host B -> Switch1 -> Switch2 -> Host C
    UdpEchoClientHelper echoClientB(hostInterfaces.GetAddress(2), 9);
    echoClientB.SetAttribute("MaxPackets", UintegerValue(2));
    echoClientB.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClientB.SetAttribute("PacketSize", UintegerValue(512));

    ApplicationContainer clientAppsB = echoClientB.Install(hosts.Get(1));
    clientAppsB.Start(Seconds(2.0));
    clientAppsB.Stop(Seconds(10.0));

    // ========== 步骤 8: 运行仿真 ==========
    NS_LOG_INFO("=== Starting Simulation ===");

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_INFO("=== Simulation Complete ===");

    return 0;
}

/*
 * ============================================================================
 * 【关键设计总结】
 * ============================================================================
 *
 * 1. 多交换机拓扑：
 *    - 3 个交换机组成链式拓扑
 *    - 每个交换机独立运行 L2SwitchProtocol
 *    - 数据包通过多跳转发到达目的地
 *
 * 2. 使用协议栈架构：
 *    - L2SwitchProtocol 继承 Object，而不是 NetDevice
 *    - 通过 node->AggregateObject() 聚合到节点
 *    - 可以通过 node->GetObject<L2SwitchProtocol>() 获取
 *
 * 3. 自定义协议实现转发：
 *    - 不依赖 ns-3 内置的 BridgeNetDevice
 *    - L2SwitchProtocol 通过混杂模式回调接收所有数据包
 *    - 协议自主完成 MAC 学习和转发决策
 *
 * 4. 为什么使用 CSMA 而不是 Point-to-Point：
 *    - Point-to-Point 链路会修改目的 MAC 地址为对端设备的 MAC
 *    - CSMA (以太网) 链路保持原始的目的 MAC 地址不变
 *    - 这对于二层交换机的正确工作至关重要
 *
 * 5. 数据包转发路径示例：
 *    Host A -> Host C:
 *      Host A -> Switch0 -> Switch1 -> Switch2 -> Host C
 *    Host B -> Host C:
 *      Host B -> Switch1 -> Switch2 -> Host C 
 *
 * ============================================================================
 */
