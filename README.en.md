# NS3-Learning: Data Center Network Simulation Learning Project

[![NS-3 Version](https://img.shields.io/badge/NS--3-3.44-blue.svg)](https://www.nsnam.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

## 📖 Introduction

This is a data center network simulation learning project based on **NS-3 (Network Simulator 3)**, focusing on the implementation and performance analysis of **Fat-Tree topology**.

The project includes comparative studies of multiple Fat-Tree implementation approaches:
- Standard implementation based on global ECMP routing
- Improved implementation based on static route aggregation
- Complete performance testing and visualization analysis

## 🏗️ Project Structure

```
ns3-learning/
├── Fat-Tree/                          # Fat-Tree implementations
│   ├── DCN_FatTree_CSMA.cc           # ECMP version (global routing)
│   ├── DCN_FatTree_Custom.cc         # Static routing version (route aggregation)
│   ├── DCN_FatTree_代码讲解.md         # ECMP version detailed explanation (Chinese)
│   └── DCN_FatTree_Custom_代码讲解.md  # Static routing version detailed explanation (Chinese)
├── README.md                          # Project description (Chinese)
└── README.en.md                       # Project description (English)
```

## 🚀 Quick Start

### Prerequisites

- **NS-3**: Version 3.44
- **Compiler**: GCC 7.0+ or Clang 6.0+
- **OS**: Linux (Ubuntu 18.04+ recommended)
- **Dependencies**: Python 3.6+, SQLite3, etc.

### Installation Steps

1. **Download and build NS-3**
   ```bash
   cd /path/to/ns3
   ./ns3 configure --enable-examples --enable-tests
   ./ns3 build
   ```

2. **Clone this repository**
   ```bash
   git clone https://gitee.com/your-username/ns3-learning.git
   cd ns3-learning
   ```

3. **Run simulations**
   ```bash
   # ECMP version (recommended)
   ./ns3 run DCN_FatTree_CSMA

   # Static routing version
   ./ns3 run DCN_FatTree_Custom
   ```

## 📊 Key Features

### Fat-Tree Implementation Comparison

| Feature | ECMP Version | Static Routing Version |
|---------|-------------|----------------------|
| **Routing Algorithm** | Global ECMP | Static route aggregation |
| **IP Assignment** | Regularized /30 subnets | Improved /30 subnets |
| **Performance** | Better latency | Compact routing table |
| **Complexity** | Medium | Medium-high |
| **Use Cases** | Production simulation | Research learning |

### Simulation Results

- ✅ **Connectivity**: 4 pods fully connected
- ✅ **Packet Loss**: 0%
- ✅ **Latency**: Microsecond level (< 50μs)
- ✅ **ECMP Load Balancing**: Traffic automatically distributed across multiple paths

## 📚 Learning Resources

### Core Documentation

- [**Fat-Tree ECMP Version Details**](Fat-Tree/DCN_FatTree_代码讲解.md)
- [**Fat-Tree Static Routing Version Details**](Fat-Tree/DCN_FatTree_Custom_代码讲解.md)

### Key Concepts

- **Fat-Tree Topology**: k=4 three-layer architecture with (k/2)² = 4 core switches
- **ECMP Routing**: Equal Cost Multi-Path load balancing
- **FlowMonitor**: NS-3 traffic monitoring and statistics
- **NetAnim**: Network animation visualization

## 🔧 Usage Instructions

### Runtime Parameters

```bash
# Disable ECMP (ECMP version only)
./ns3 run "DCN_FatTree_CSMA --ECMProuting=false"

# Debug with GDB
./ns3 run DCN_FatTree_CSMA --gdb

# View detailed logs
NS_LOG="DCN_FatTree_Simulation=level_info" ./ns3 run DCN_FatTree_CSMA
```

### Output Files

- `*.flowmon`: FlowMonitor statistics data
- `animation.xml`: NetAnim visualization file
- `*.pcap`: Wireshark packet capture files

## 🤝 Contributing

Issues and Pull Requests are welcome!

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Create a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details

## 👨‍💻 Author

**Ceylan Liu** - *Initial development*
- Email: 1575373800@qq.com
- Gitee: [ceylanliu](https://gitee.com/ceylanliu)

## 🙏 Acknowledgments

- [NS-3 Official Documentation](https://www.nsnam.org/documentation/)
- [Fat-Tree Original Paper](https://ccr.sigcomm.org/online/files/p63-alfares.pdf)
- Gitee platform for code hosting services

---

⭐ If this project helps you, please give it a star!
