# 1. Mathematical Theory & Post-Training Quantization (PTQ)

This document details the mathematical framework of the sequential Convolutional Neural Network (CNN) hardware accelerator built for MNIST digit classification, its training pipeline in Google Colab, and the fixed-scale Post-Training Quantization (PTQ) calibration strategy used to map floating-point operations onto hardware fabric safely.

---

## 1. Network Architecture Specifications

To achieve efficient execution within the constraints of the Xilinx Zynq-7000 SoC and target full parallel execution, a highly compressed sequential CNN architecture was selected. The network uses a total of 5,258 trainable parameters, which fits completely within the on-chip Block RAM (BRAM) and DSP blocks without requiring external memory sweeps for weights during live streaming inference.

The structural breakdown of the layer configurations, shapes, and parameter counts is detailed below:

| Layer Name | Operation Type | Input Shape | Output Shape | Activation Function | Trainable Parameters |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **Input** | Data Buffer | N/A | 28x28x1 | None | 0 |
| **conv_1** | 2D Convolution (3x3 Kernel) | 28x28x1 | 26x26x8 | ReLU | 80 (72 Weights + 8 Biases) |
| **pool_1** | Max Pooling (2x2 Window) | 26x26x8 | 13x13x8 | None | 0 |
| **conv_2** | 2D Convolution (3x3 Kernel) | 13x13x8 | 11x11x16 | ReLU | 1,168 (1,152 W + 16 B) |
| **pool_2** | Max Pooling (2x2 Window) | 11x11x16 | 5x5x16 | None | 0 |
| **Flatten** | Dimensional Unrolling | 5x5x16 | 400 | None | 0 |
| **output_dense** | Fully Connected (Dense) | 400 | 10 | None (Raw Logits) | 4,010 (4,000 W + 10 B) |
| **Total** | **Full Network Core** | | | | **5,258 Parameters** |

For the complete layout of how these layers translate into synthesizable C++ loops, optimize pipelines, and utilize temporal unrolling, please refer to the structural details in [2. HLS Core Architecture](2_HLS_Architecture.md).

---

## 2. Google Colab Training Environment

The model baseline was developed in Python using TensorFlow/Keras on Google Colab to track training performance and derive optimized floating-point array matrices before starting hardware quantization. The source scripts can be found in the repository root folder under [`/software_training/`](../software_training/).

### Training Configuration
* **Dataset:** MNIST Digits containing 60,000 training samples and 10,000 testing samples.
* **Preprocessing:** Input pixels scaled linearly from integer ranges into standard floating-point numbers between 0.0 and 1.0.
* **Loss Function:** Categorical crossentropy configured from logits. Omitting the Softmax activation function in hardware ensures numerical stability and prevents complex division/exponentiation blocks from hogging FPGA logic.
* **Baseline Validation Results:** The floating-point Python baseline achieved a high-accuracy reference profile, serving as our golden model.

---

## 3. Post-Training Quantization (PTQ) & Calibration Strategy

Standard FPGAs do not natively compute floating-point operations efficiently; mapping raw variables directly onto hardware requires significant fabric area and ruins processing throughput. To resolve this, a uniform Fixed-Scale Post-Training Quantization (PTQ) routine was designed.

### Bit-Width Allocation and Typing
To preserve maximum accuracy while optimizing hardware space, the final working implementation utilizes a strict mixed bit-width allocation layout to govern the streaming data:
* **Weights:** Stored as strict 8-bit signed integers (`int8`) to save logic space and match internal DSP register structures.
* **Input Pixels:** Strictly processed as 8-bit unsigned integers (`ap_uint<8>`). This ensures that peak white pixels containing a raw value of 255 do not experience sign-bit wraparound to -1, which would distort image data. Input pixels are cast to signed 32-bit values only at the exact moment of arithmetic multiplication.
* **Biases and Accumulators:** Allocated as 32-bit signed integers (`int32`) to match the dynamic ranges of the computing nodes and provide vast numerical headroom.

### Activation Calibration and Re-Quantization
Rather than allowing scaling factors to exponentially grow across the layers, the architecture utilizes an explicit calibration phase. A post-training calibration step was performed on 1,000 representative training images to determine the global maximum activations at each stage. These values were used to derive fixed, static inter-layer re-quantization scaling multipliers:
* **Layer 1 Scaling Factor (`L1_SCALE`):** Evaluated at a static value of `0.001327`.
* **Layer 2 Scaling Factor (`L2_SCALE`):** Evaluated at a static value of `0.002090`.

At the end of each layer pipeline, the hardware multiplies the intermediate 32-bit accumulated sum by these pre-computed constants. This safely scales and re-quantizes the intermediate data paths back down into the standard 8-bit range before feeding the data forward into the subsequent processing block. 

*(Note: The narrative and troubleshooting data tracking how uniform exponential scale factors initially saturated intermediate data ranges and caused execution anomalies are fully logged in [7. Debugging Issues](7_Debugging_Issues.md) under Bug 1).*

---

## 4. Arithmetic Protection Layout: 32-bit Accumulators

The final hardware implementation relies on a highly efficient data path designed to eliminate arithmetic distortion. During execution, the unrolled parallel loop operations across the convolutional and dense layers carry out heavy dot-product calculations. 

By mapping the mathematical operations into 32-bit signed integer accumulation registers, the core provides extensive numerical safety boundaries. This structure completely eliminates intermediate arithmetic overflow during parallel runtime loops, preserving a high fixed-point hardware deployment accuracy of 97.00% over the software baseline.

---

## 5. C++ Header Extraction Method

An automated extraction script isolates the optimized weights and biases from the trained model, scales them using the calibrated PTQ parameters, and formats them into static hardware-friendly arrays.

This process converts the multi-dimensional parameter blocks into static arrays within a standard header file ([`weights_quant.h`](../hls_core/weights_quant.h)), storing weights as 8-bit signed integers and biases as 32-bit signed integers, making the array values instantly readable by the High-Level Synthesis toolchain. The resulting parameters are hardcoded into the FPGA's internal lookup structures during synthesis. This guarantees single-cycle parameter access speeds without requiring any runtime external memory lookups.
