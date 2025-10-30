#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace ns3;
using namespace ns3::SystemPath;

// ===================== Globals =====================
std::string dir;
std::ofstream throughput1;
std::ofstream throughput2;
std::ofstream queueSize;
std::ofstream jainIndex;

Time prevTime1 = Seconds(0);
Time prevTime2 = Seconds(0);
uint64_t prevTxBytes1 = 0; // עדיף 64-ביט לסימולציות ארוכות
uint64_t prevTxBytes2 = 0;

Ptr<Ipv4FlowClassifier> classifier;

// MSS דינמי עבור הדפסות cwnd ב-MSS
static uint32_t g_mssBytes = 1448;

// ===================== Helpers / Metrics =====================
static double JainIndex2(double x1, double x2)
{
    double denom = 2.0 * (x1 * x1 + x2 * x2);
    if (denom <= 0.0) return 0.0;
    double sum = x1 + x2;
    return (sum * sum) / denom;
}

// ===================== Tracers =====================
static void TraceThroughput(Ptr<FlowMonitor> monitor)
{
    auto stats = monitor->GetFlowStats();
    Time now = Now();

    double r1 = -1.0; // Mbps (ערך שלילי = לא חושב בדגימה)
    double r2 = -1.0;

    for (const auto& kv : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);

        if (t.sourceAddress == Ipv4Address("10.0.2.1"))
        {
            double rateMbps = 0.0;
            if (now > prevTime1)
            {
                double dUs = (now - prevTime1).ToDouble(Time::US);
                rateMbps = 8.0 * (static_cast<double>(kv.second.txBytes - prevTxBytes1)) / dUs; // bit/us = Mb/s
            }
            throughput1 << now.GetSeconds() << " " << rateMbps << " Mbps" << std::endl;
            prevTxBytes1 = kv.second.txBytes;
            prevTime1 = now;
            r1 = rateMbps;
        }
        else if (t.sourceAddress == Ipv4Address("10.0.3.1"))
        {
            double rateMbps = 0.0;
            if (now > prevTime2)
            {
                double dUs = (now - prevTime2).ToDouble(Time::US);
                rateMbps = 8.0 * (static_cast<double>(kv.second.txBytes - prevTxBytes2)) / dUs;
            }
            throughput2 << now.GetSeconds() << " " << rateMbps << " Mbps" << std::endl;
            prevTxBytes2 = kv.second.txBytes;
            prevTime2 = now;
            r2 = rateMbps;
        }
    }

    if (r1 < 0.0) r1 = 0.0;
    if (r2 < 0.0) r2 = 0.0;
    double j = JainIndex2(r1, r2);
    //jainIndex << now.GetSeconds() << " " << j << std::endl;
    jainIndex << j << std::endl;
    Simulator::Schedule(Seconds(0.2), &TraceThroughput, monitor);
}

void CheckQueueSize(Ptr<QueueDisc> qd)
{
    uint32_t qsize = qd->GetCurrentSize().GetValue();
    queueSize << Simulator::Now().GetSeconds() << " " << qsize << " packets" << std::endl;
    Simulator::Schedule(Seconds(0.2), &CheckQueueSize, qd);
}

static void CwndTracer(Ptr<OutputStreamWrapper> stream, uint32_t /*oldval*/, uint32_t newval)
{
    double cwndInMss = static_cast<double>(newval) / static_cast<double>(g_mssBytes);
    *stream->GetStream() << Simulator::Now().GetSeconds() << " "
                         << cwndInMss << " MSS" << std::endl;
}

// label = "0" or "1" -> cwnd0.dat / cwnd1.dat
void TraceCwndLabeled(uint32_t nodeId, uint32_t socketId, const std::string& label)
{
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream =
        ascii.CreateFileStream(dir + "/cwnd" + label + ".dat");
    Config::ConnectWithoutContext("/NodeList/" + std::to_string(nodeId) +
                                  "/$ns3::TcpL4Protocol/SocketList/" +
                                  std::to_string(socketId) + "/CongestionWindow",
                                  MakeBoundCallback(&CwndTracer, stream));
}

// ===================== main =====================
int main(int argc, char* argv[])
{
    // ----- Timestamped results dir -----
    char buffer[80];
    time_t rawtime; time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%d-%m-%Y-%I-%M-%S", timeinfo);
    std::string currentTime(buffer);

    // ===== CLI params =====
    double rttMs = 0.0;                 // אם >0, נחשב דיליי לכל לינק לפי יחס edge:bneck=1:2
    double edgeDelayMs = 5.0;           // דיליי לקישורי קצה (אם rttMs==0)
    double bottleneckDelayMs = 10.0;    // דיליי לצוואר הבקבוק (אם rttMs==0)
    std::string dataRateEdge = "1000Mbps";
    std::string dataRateBottleneck = "10Mbps";

    uint32_t mssBytes = 1448;           // TCP Segment Size
    double start1Sec = 0;             // זמן התחלה לשולח 1
    double start2Sec = 0;             // זמן התחלה לשולח 2
    
    
    //default protocols
    std::string tcp1 = "ns3::TcpNewReno"; // אלגוריתם TCP לשולח 1
    std::string tcp2 = "ns3::TcpNewReno"; // אלגוריתם TCP לשולח 2
    uint32_t delAckCount = 2;
    Time stopTime = Seconds(120);

    CommandLine cmd(__FILE__);
    cmd.AddValue("rttMs", "Total desired RTT in ms (overrides per-link delays; edge:bneck=1:2).", rttMs);
    cmd.AddValue("edgeDelayMs", "Edge one-way delay in ms (used if rttMs==0).", edgeDelayMs);
    cmd.AddValue("bottleneckDelayMs", "Bottleneck one-way delay in ms (used if rttMs==0).", bottleneckDelayMs);
    cmd.AddValue("dataRateEdge", "Edge link DataRate, e.g. 1000Mbps.", dataRateEdge);
    cmd.AddValue("dataRateBottleneck", "Bottleneck DataRate, e.g. 10Mbps.", dataRateBottleneck);

    cmd.AddValue("mssBytes", "TCP segment size (bytes).", mssBytes);
    cmd.AddValue("start1", "Sender1 start time [s].", start1Sec);
    cmd.AddValue("start2", "Sender2 start time [s].", start2Sec);

    cmd.AddValue("tcp1", "TCP type for sender1 (e.g. ns3::TcpNewReno, ns3::TcpBbr).", tcp1);
    cmd.AddValue("tcp2", "TCP type for sender2 (e.g. ns3::TcpNewReno, ns3::TcpBbr).", tcp2);
    cmd.AddValue("delAckCount", "TCP Delayed ACK count.", delAckCount);
    cmd.AddValue("stopTime", "Applications stop time (sim stops at stopTime+1 tick).", stopTime);
    cmd.Parse(argc, argv);

    // חישוב דיליי לפי RTT אם התבקש
    if (rttMs > 0.0) {
        // RTT ≈ 4*edge + 2*bneck ; נבחר יחס edge : bneck = 1 : 2 ⇒ RTT ≈ 8*edge
        double edge = rttMs / 8.0;
        edgeDelayMs = edge;
        bottleneckDelayMs = 2.0 * edge;
    }

    // ----- TCP defaults (general) -----
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4194304));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(6291456));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(mssBytes));

    // עדכון MSS גלובלי לטרייס של cwnd
    g_mssBytes = mssBytes;

    // ----- Topology -----
    NodeContainer sender, sender2, receiver, receiver2, routers;
    sender.Create(1);
    sender2.Create(1);
    receiver.Create(1);
    receiver2.Create(1);
    routers.Create(2); // R1, R2

    PointToPointHelper bottleneckLink, edgeLinkSender, edgeLinkSender2, edgeLinkReceiver, edgeLinkReceiver2;

    bottleneckLink.SetDeviceAttribute("DataRate", StringValue(dataRateBottleneck));
    bottleneckLink.SetChannelAttribute("Delay", StringValue(std::to_string(bottleneckDelayMs) + "ms"));

    edgeLinkSender.SetDeviceAttribute("DataRate", StringValue(dataRateEdge));
    edgeLinkSender.SetChannelAttribute("Delay", StringValue(std::to_string(edgeDelayMs) + "ms"));
    edgeLinkSender2.SetDeviceAttribute("DataRate", StringValue(dataRateEdge));
    edgeLinkSender2.SetChannelAttribute("Delay", StringValue(std::to_string(edgeDelayMs) + "ms"));
    edgeLinkReceiver.SetDeviceAttribute("DataRate", StringValue(dataRateEdge));
    edgeLinkReceiver.SetChannelAttribute("Delay", StringValue(std::to_string(edgeDelayMs) + "ms"));
    edgeLinkReceiver2.SetDeviceAttribute("DataRate", StringValue(dataRateEdge));
    edgeLinkReceiver2.SetChannelAttribute("Delay", StringValue(std::to_string(edgeDelayMs) + "ms"));

    NetDeviceContainer senderEdge   = edgeLinkSender.Install(sender.Get(0),  routers.Get(0)); // R1 dev 0
    NetDeviceContainer sender2Edge  = edgeLinkSender2.Install(sender2.Get(0), routers.Get(0)); // R1 dev 1
    NetDeviceContainer r1r2         = bottleneckLink.Install(routers.Get(0), routers.Get(1)); // R1 dev 2, R2 dev 0
    NetDeviceContainer receiverEdge = edgeLinkReceiver.Install(routers.Get(1), receiver.Get(0)); // R2 dev 1
    NetDeviceContainer receiverEdge2= edgeLinkReceiver2.Install(routers.Get(1), receiver2.Get(0)); // R2 dev 2

    // ----- Internet stack -----
    InternetStackHelper internet;
    internet.Install(sender);
    internet.Install(sender2);
    internet.Install(receiver);
    internet.Install(receiver2);
    internet.Install(routers);

    // ----- TCP per sender (override per-node) -----
    uint32_t snd1Id = sender.Get(0)->GetId();
    uint32_t snd2Id = sender2.Get(0)->GetId();
    Config::Set ("/NodeList/" + std::to_string(snd1Id) + "/$ns3::TcpL4Protocol/SocketType",
                 TypeIdValue (TypeId::LookupByName (tcp1)));
    Config::Set ("/NodeList/" + std::to_string(snd2Id) + "/$ns3::TcpL4Protocol/SocketType",
                 TypeIdValue (TypeId::LookupByName (tcp2)));

    // ----- IP addressing (distinct /24 per link) -----
    Ipv4AddressHelper ipv4;

    ipv4.SetBase("10.0.1.0", "255.255.255.0"); // R1<->R2
    Ipv4InterfaceContainer i1i2 = ipv4.Assign(r1r2); // R1:10.0.1.1 , R2:10.0.1.2

    ipv4.SetBase("10.0.2.0", "255.255.255.0"); // Sender1<->R1
    Ipv4InterfaceContainer is1 = ipv4.Assign(senderEdge); // sender1:10.0.2.1 , R1:10.0.2.2

    ipv4.SetBase("10.0.3.0", "255.255.255.0"); // Sender2<->R1
    Ipv4InterfaceContainer is2 = ipv4.Assign(sender2Edge); // sender2:10.0.3.1 , R1:10.0.3.2

    ipv4.SetBase("10.0.4.0", "255.255.255.0"); // R2<->Receiver1
    Ipv4InterfaceContainer ir1 = ipv4.Assign(receiverEdge); // R2:10.0.4.1 , recv1:10.0.4.2

    ipv4.SetBase("10.0.5.0", "255.255.255.0"); // R2<->Receiver2
    Ipv4InterfaceContainer ir2 = ipv4.Assign(receiverEdge2); // R2:10.0.5.1 , recv2:10.0.5.2

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // ----- Results dir & files -----
    //dir = "2FLOWS-FINAL-results/" + currentTime + "/";
    dir = "2FLOWS-FINAL-results";
    MakeDirectories(dir);

    throughput1.open(dir + "/throughput1.dat", std::ios::out);
    throughput2.open(dir + "/throughput2.dat", std::ios::out);
    queueSize.open(dir + "/queueSize.dat", std::ios::out);
    jainIndex.open(dir + "/jain.dat", std::ios::out);
    NS_ASSERT_MSG(throughput1.is_open(), "Throughput1 file was not opened correctly");
    NS_ASSERT_MSG(throughput2.is_open(), "Throughput2 file was not opened correctly");
    NS_ASSERT_MSG(queueSize.is_open(),   "Queue size file was not opened correctly");
    NS_ASSERT_MSG(jainIndex.is_open(),   "Jain index file was not opened correctly");

    // ----- Applications -----
    uint16_t port1 = 50001;
    BulkSendHelper source1("ns3::TcpSocketFactory", InetSocketAddress(ir1.GetAddress(1), port1));
    source1.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer appSrc1 = source1.Install(sender.Get(0));
    appSrc1.Start(Seconds(start1Sec));
    appSrc1.Stop(stopTime);

    PacketSinkHelper sink1("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port1));
    ApplicationContainer appSnk1 = sink1.Install(receiver.Get(0));
    appSnk1.Start(Seconds(0.0));
    appSnk1.Stop(stopTime);

    uint16_t port2 = 50002;
    BulkSendHelper source2("ns3::TcpSocketFactory", InetSocketAddress(ir2.GetAddress(1), port2));
    source2.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer appSrc2 = source2.Install(sender2.Get(0));
    appSrc2.Start(Seconds(start2Sec));
    appSrc2.Stop(stopTime);

    PacketSinkHelper sink2("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port2));
    ApplicationContainer appSnk2 = sink2.Install(receiver2.Get(0));
    appSnk2.Start(Seconds(0.0));
    appSnk2.Stop(stopTime);

    // ----- cwnd traces -----
    Simulator::Schedule(Seconds(start1Sec) + MilliSeconds(1),
        &TraceCwndLabeled, sender.Get(0)->GetId(), 0, std::string("0"));
    Simulator::Schedule(Seconds(start2Sec) + MilliSeconds(1),
        &TraceCwndLabeled, sender2.Get(0)->GetId(), 0, std::string("1"));

    // ----- QueueDisc at bottleneck (R1 side, device index 2) with safety -----
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc");
    Ptr<NetDevice> devR1 = routers.Get(0)->GetDevice(2);
    Ptr<TrafficControlLayer> tcR1 = routers.Get(0)->GetObject<TrafficControlLayer>();
    Ptr<QueueDisc> qdiscR1 = nullptr;
    if (tcR1)
    {
        qdiscR1 = tcR1->GetRootQueueDiscOnDevice(devR1);
    }
    if (!qdiscR1)
    {
        QueueDiscContainer qdc = tch.Install(devR1);
        qdiscR1 = qdc.Get(0);
    }
    Simulator::ScheduleNow(&CheckQueueSize, qdiscR1);

    // ----- FlowMonitor & periodic throughput sampling -----
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    Simulator::Schedule(Seconds(0.2), &TraceThroughput, monitor);

    // (Optional) print flows once
    Simulator::Schedule(Seconds(1.0), [monitor]() {
        for (auto const& kv : monitor->GetFlowStats()) {
            auto t = classifier->FindFlow(kv.first);
            std::cout << "Flow " << kv.first << " src=" << t.sourceAddress
                      << " dst=" << t.destinationAddress
                      << " proto=" << (uint16_t)t.protocol << std::endl;
        }
    });

    Simulator::Stop(stopTime + TimeStep(1));
    Simulator::Run();
    Simulator::Destroy();

    throughput1.close();
    throughput2.close();
    queueSize.close();
    jainIndex.close();

    return 0;
}
