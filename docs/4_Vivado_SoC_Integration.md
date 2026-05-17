# 4. Vivado SoC Integration

This document details the system-level integration of the synthesized High-Level Synthesis (HLS) CNN core into the Xilinx Vivado Block Design. It covers the structural interconnect wiring, Direct Memory Access (DMA) channel configurations, memory-mapped routing paths, and system-level clock management.

---

## 1. System Topology Overview

The SoC integration establishes a clean master-slave infrastructure, coupling the high-performance ARM Cortex-A9 processing system with the parallel hardware accelerator core through the Xilinx AXI4 ecosystem. 

The system layout is split into two distinct operational networks:
* **Memory-Mapped Control Bus (AXI4-Lite):** Operates as a register-management channel. The ARM host CPU acts as the AXI master, sending control tokens down to the accelerator core and the DMA management engines.
* **High-Speed Direct Data Stream (AXI4-Stream):** Bypasses the central CPU registers entirely. The memory transactions travel through a dedicated hardware channel to pump raw pixel frames directly out of the central DDR memory into the FPGA fabric, and to retrieve final scoring queues back into memory.

Below is the complete hardware architectural layout showing the interconnect lines and block integration mapping:

![Vivado Block Diagram](../docs/cnn_accel_vivado_bd.png)

---

## 2. Block Design Connectivity and Port Routing

The complete hardware system configuration is established by linking specific hardware blocks inside the Vivado block layout view. 

### A. Processing System Setup (Zynq7 Processing System)
The Zynq-7000 hard intellectual property (IP) block serves as the processing anchor.
* **Control Port:** The General Purpose Master AXI interface (`M_AXI_GP0`) is activated to drive the runtime configuration commands down to the peripheral registers.
* **Memory Port:** The High-Performance Slave AXI interface (`S_AXI_HP0`) is enabled. This dedicated port grants the external DMA engine direct, high-bandwidth access to the shared onboard DDR memory controller, bypassing the main CPU cache to prevent memory bus contention.
* **Hardware Interrupts:** The Programmable Logic to Processing System (PL-PS) Fabric Interrupt port (`IRQ_F2P`) is enabled. This receives the `s2mm_introut` signal from the DMA, allowing the host ARM processor to asynchronously wait for inference completion without wasting CPU cycles on polling.

### B. Direct Memory Access Configuration (AXI DMA)
An AXI Direct Memory Access (DMA) core bridges the memory-mapped storage domains and the streaming hardware logic gates.
* **Direct Register Mode:** The DMA core is configured with Scatter-Gather engine elements disabled. This reduces block resource utilization and forces the engine to run in Direct Register Mode.
* **Buffer & Width Constraints:** To prevent data misalignment and `TDATA_NUM_BYTES` metadata errors, "Allow Unaligned Transfers" is explicitly disabled, and the Stream Data Width is strictly locked to 32 bits for both channels. Furthermore, the Width of the Buffer Length Register is expanded to 26 bits, providing enough headroom for transfers up to 64MB without risk of truncation.
* **Channel Configuration:** The block establishes a full-duplex communication pipe:
  * The Read Channel (`M_AXI_MM2S`) reads packed 32-bit pixel streams out of the DDR and converts them into an active AXI4-Stream output (`M_AXIS_MM2S`).
  * The Write Channel (`M_AXI_S2MM`) accepts incoming 32-bit hardware classification scoring streams (`S_AXIS_S2MM`) and writes them sequentially into a destination memory array, firing the `s2mm_introut` interrupt upon completion.

### C. Accelerator IP Core Interface Layout (`cnn_accel`)
The compiled HLS accelerator core sits between the inbound and outbound streaming interfaces of the DMA core:
* The input stream port (`S_AXIS`) connects directly to the DMA's memory-to-stream (`M_AXIS_MM2S`) transmission channel.
* The output stream port (`M_AXIS`) connects directly to the DMA's stream-to-memory-mapped (`S_AXIS_S2MM`) capture channel.
* The runtime control interface port (`s_axi_CONTROL_BUS`) hooks into the peripheral Interconnect network driven by the processing system's `M_AXI_GP0` interface.

---

## 3. Peripheral Interconnect and Bridge Infrastructure

To resolve bus protocol differences and balance separate clock domain crossings, standard layout helper modules are integrated into the final design:

* **AXI Interconnect Slices:** Two independent interconnect blocks manage communication traffic. The SmartConnect/Interconnect core on the control side arbitrates multiple AXI4-Lite channels. The memory-side Interconnect aggregates the DMA's master read/write paths, funneling them into the single `S_AXI_HP0` interface on the Zynq wrapper.
* **Processor System Reset Core:** Generates synchronized operational reset signals (`peripheral_reset` and `interconnect_aresetn`) mapped to the core fabric. This guarantees that all registers clear synchronously when the host triggers a system reboot or load cycle.

---

## 4. Clock Distribution Management

System timing is governed by a central hardware-locked clock path:
* **Source Frequency:** The system routes a single 100 MHz reference clock source generated directly by the Zynq Processing System's Phase-Locked Loop block via the `FCLK_CLK0` pin.
* **Unified Distribution Network:** This 100 MHz clock source drives all attached system blocks simultaneously—including the ARM peripheral interfaces, the AXI DMA engine cores, the interconnect slices, and the `cnn_accel` internal multi-accumulation calculation pipelines. 
* **Timing Assurance:** Enforcing a single unified clock domain removes the need for asynchronous clock-crossing FIFO cells, minimizing routing complexity and layout area across the programmable logic.
