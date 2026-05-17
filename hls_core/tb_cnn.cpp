#include <iostream>
#include "cnn_accel.h"
#include "image_test.h"  // Contains 'test_img' array and 'TEST_LABEL'

int main() {
    // 1. Declare Streams
    hls::stream<axis_t> in_stream;
    hls::stream<axis_t> out_stream;

    std::cout << "=============================================" << std::endl;
    std::cout << "      MNIST CNN HLS C-SIMULATION            " << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << ">> Loading Image... (Expected Label: " << TEST_LABEL << ")" << std::endl;

    // 2. Feed the Image to the Accelerator
    // The loop sends 784 pixels (28x28) one by one
    for(int i = 0; i < 784; i++) {
        axis_t val;
        
        // Load pixel from the header file.
        // It's 'unsigned char' (0-255), so casting to ap_uint<32> is safe.
        val.data = (ap_uint<32>)test_img[i];
        
        // AXI Stream Side-Channel Signals (Standard Defaults)
        val.keep = 0xF; 
        val.strb = 0xF;
        val.last = (i == 783) ? 1 : 0; // Assert TLAST on the final pixel

        in_stream.write(val);
    }

    // 3. Run the Hardware Function
    std::cout << ">> Running Hardware Acceleration..." << std::endl;
    cnn_accel(in_stream, out_stream);

    // 4. Read and Parse Results
    std::cout << ">> Reading Results..." << std::endl;
    
    int max_score = -2147483648; // Initialize with smallest possible integer
    int pred_class = -1;

    // The IP outputs 10 scores (one for each digit 0-9)
    for(int i = 0; i < 10; i++) {
        if(out_stream.empty()) {
            std::cout << "ERROR: Output stream is empty at index " << i << "!" << std::endl;
            break;
        }

        axis_t out_pkt = out_stream.read();
        
        // The output data is a 32-bit integer score (accumulated value)
        int score = (int)out_pkt.data;
        
        // Print the score for debugging
        std::cout << "   Class " << i << " Score: " << score << std::endl;

        // Find the maximum score (Argmax)
        if (score > max_score) {
            max_score = score;
            pred_class = i;
        }
    }

    // 5. Final Verification
    std::cout << "---------------------------------------------" << std::endl;
    std::cout << "   Predicted Class: " << pred_class << std::endl;
    std::cout << "   Expected Class:  " << TEST_LABEL << std::endl;
    std::cout << "---------------------------------------------" << std::endl;

    if (pred_class == TEST_LABEL) {
        std::cout << ">> SUCCESS: Simulation Passed!" << std::endl;
        return 0; // Return 0 indicates success to Vivado HLS
    } else {
        std::cout << ">> FAILURE: Prediction mismatch." << std::endl;
        return 1; // Return 1 indicates failure
    }
}