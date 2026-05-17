# 1. Mathematical Theory & Post-Training Quantization (PTQ)

This document details the mathematical framework of the sequential Convolutional Neural Network (CNN) hardware accelerator built for MNIST digit classification, its training pipeline in Google Colab, and the fixed-scale Post-Training Quantization (PTQ) strategy used to map floating-point operations onto hardware fabric safely.

---

## 1. Network Architecture Specifications

To achieve efficient execution within the constraints of the Xilinx Zynq-7000 SoC and target full parallel execution, a highly compressed 2-layer sequential CNN architecture was selected. The network uses a total of 5,258 trainable parameters, which fits completely within the on-chip Block RAM (BRAM) and DSP blocks without requiring external memory sweeps for weights during live streaming inference.

The hardware network architecture is structured sequentially as follows:

* **Input Layer:** Accepts input images structured as a 3D tensor with the shape 28x28x1 grayscale.
* **Convolutional Layer 1 (conv_1):** Applies 8 filters using a 3x3 kernel with an activation of ReLU (Rectified Linear Unit). The output shape before pooling is 26x26x8. It then applies a Max Pooling operation using a 2x2 window, which reduces the spatial dimensions to an output feature map of 13x13x8. This layer contains 80 parameters.
* **Convolutional Layer 2 (conv_2):** Directly ingests the 13x13x8 feature map and applies 16 filters using a 3x3 kernel with a ReLU activation. The output shape before pooling is 11x11x16. It then applies a Max Pooling operation using a 2x2 window, which further shrinks the feature map to 5x5x16. This layer contains 1,168 parameters.
* **Fully Connected Output Layer (output_dense):** Flattens the 5x5x16 3D feature map into a 1-dimensional vector containing 400 elements. This vector feeds into a fully connected dense layer with 10 units, corresponding to the prediction classes for digits 0-9. This layer contains 4,010 parameters.

---

## 2. Google Colab Training Environment

The model baseline was developed in Python using TensorFlow/Keras on Google Colab to track training performance and derive optimized floating-point array matrices before starting hardware quantization.

### Training Configuration
* **Dataset:** MNIST Digits containing 60,000 training samples and 10,000 testing samples.
* **Preprocessing:** Input pixels scaled linearly from integer ranges into standard floating-point numbers between 0.0 and 1.0.
* **Loss Function:** Categorical crossentropy configured from logits. Omitting the Softmax activation function in hardware ensures numerical stability and prevents complex division/exponentiation blocks from hogging FPGA logic.
* **Baseline Validation Results:** The floating-point Python baseline achieved a high-accuracy reference profile, serving as our golden model.

---

## 3. Post-Training Quantization (PTQ) Strategy

Standard FPGAs do not natively compute floating-point operations efficiently; mapping raw variables directly onto hardware requires significant fabric area and ruins processing throughput. To resolve this, a uniform Fixed-Scale Post-Training Quantization (PTQ) routine was designed.

### Fixed-Point Scale Mapping
Weights, biases, and streaming data items were mapped into signed integers using symmetric scaling equations where the floating-point value is multiplied by a scale factor and rounded to the nearest integer. During characterization sweeps across the parameter ranges, a fixed scaling factor of 127 was chosen to align with the boundaries of an 8-bit signed integer.

### Bit-Width Allocation and Scaling Targets
To preserve maximum accuracy while optimizing hardware space, the final working implementation utilizes a strict mixed bit-width allocation layout:
* **Weights:** Stored as strict 8-bit signed integers to save logic space and match register structures.
* **Biases:** Allocated as 32-bit signed integers to match the dynamic ranges of the computing nodes.

When multiplying an 8-bit image pixel by an 8-bit convolution weight, the scale factors multiply together. To maintain mathematical correctness across the layers, the respective layer 32-bit biases were scaled up systematically to match these higher internal dynamic ranges:
* **Convolution Bias Scaling:** Scaled by the product of the input and weight scaling factors (127 multiplied by 127 equals 16,129.0).
* **Dense Bias Scaling:** Scaled by the accumulated product of all preceding layer factors (127 multiplied by 127 multiplied by 127 equals 2,048,383.0) to prevent precision misalignment.

---

## 4. Arithmetic Protection Layout: 8-bit Inputs with 32-bit Accumulators

The final hardware implementation relies on a highly efficient data path designed to eliminate arithmetic distortion:
* All streaming weights and input arrays are restricted to strict 8-bit signed integers, which maps perfectly onto individual FPGA hardware registers.
* The internal multi-accumulation calculation blocks use 32-bit signed integers. 

Using 32-bit signed integers for the accumulators provides vast numerical headroom. This completely eliminates intermediate arithmetic overflow during parallel loops, preserving a high fixed-point hardware deployment accuracy of 97.00% over the software baseline.

---

## 5. C++ Header Extraction Method

An automated extraction script isolates the optimized weights and biases from the trained model, scales them using the calibrated PTQ parameters, and formats them into static hardware-friendly arrays.

This process converts the multi-dimensional parameter blocks into static arrays within a standard header file, storing weights as 8-bit signed integers and biases as 32-bit signed integers, making the array values instantly readable by the High-Level Synthesis toolchain. The resulting parameters are hardcoded into the FPGA's internal lookup structures during synthesis. This guarantees single-cycle parameter access speeds without requiring any runtime external memory lookups.
