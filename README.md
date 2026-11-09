# Fairness Among TCP Connections

## Overview
This project investigates fairness and performance among different TCP congestion control algorithms: **NewReno, CUBIC, DCTCP, and BBR**.

Using the **ns-3 network simulator**, we analyze how Round-Trip Time (RTT) variations affect throughput, bandwidth utilization, and fairness among competing TCP flows.

## Objectives
- Compare the behavior of different TCP congestion control algorithms.
- Investigate the impact of RTT variations on TCP fairness.
- Evaluate throughput and bandwidth utilization.
- Measure fairness using Jain's Fairness Index.

## Methodology
We designed a network topology in ns-3 where two TCP flows compete over a shared bottleneck link.

Two experimental scenarios were evaluated:

1. **Equal RTT:** Both TCP flows have the same RTT.
2. **Unequal RTT:** One TCP flow experiences a higher RTT, determined by a fixed scaling factor.

We analyzed throughput and Jain's Fairness Index to compare the behavior of the congestion control algorithms.

## Key Findings
Our simulation results indicate that:

- **BBR and DCTCP** generally demonstrated higher fairness and efficient bandwidth utilization under the evaluated network conditions.
- **NewReno and CUBIC** showed greater sensitivity to RTT variations.
- Congestion control mechanisms based on ECN or bandwidth estimation can offer advantages in maintaining fairness and efficiency.

## Technologies
- **Network Simulator:** ns-3
- **Programming:** C++
- **Protocols:** TCP NewReno, CUBIC, DCTCP, BBR
- **Metrics:** Throughput, RTT, Jain's Fairness Index

## Repository Structure
- `project code/` – Project implementation and simulation code.
- `project final/` – Final project materials.

## Conclusion
This project highlights the impact of congestion control algorithms and RTT variations on network performance, providing insights into fairness and efficient bandwidth utilization in TCP networks.
