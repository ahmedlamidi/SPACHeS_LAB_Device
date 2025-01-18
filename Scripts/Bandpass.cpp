#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>


// There is no conversion to the frequency domain since the calculations can be done in the time domain
// implements a sin function 
// implements a blackman function -> just cleans out the function we created
// implements a low pass filter
// implements a high pass filter



// Function to compute sinc(x) = sin(πx) / (πx)
double sinc(double x) {
    return x == 0 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
}

// Function to generate a Blackman window
std::vector<double> blackman(int N) {
    std::vector<double> window(N);
    for (int i = 0; i < N; ++i) {
        window[i] = 0.42 - 0.5 * std::cos(2 * M_PI * i / (N - 1)) + 
                    0.08 * std::cos(4 * M_PI * i / (N - 1));
    }
    return window;
}

// Convolution of two vectors
std::vector<double> convolve(const std::vector<double>& a, const std::vector<double>& b) {
    int n = a.size() + b.size() - 1;
    std::vector<double> result(n, 0.0);

    for (size_t i = 0; i < a.size(); ++i) {
        for (size_t j = 0; j < b.size(); ++j) {
            result[i + j] += a[i] * b[j];
        }
    }
    return result;
}

int main() {
    double fL = 0.1; // Low cutoff frequency
    double fH = 0.4; // High cutoff frequency
    double b = 0.08; // Transition band

    // Compute filter length N
    int N = static_cast<int>(std::ceil(4.0 / b));
    if (N % 2 == 0) ++N; // Make sure N is odd

    // Generate the index array n
    std::vector<double> n(N);
    for (int i = 0; i < N; ++i) {
        n[i] = i;
    }

    // Compute the low-pass filter
    std::vector<double> hlpf(N);
    for (int i = 0; i < N; ++i) {
        hlpf[i] = sinc(2 * fH * (n[i] - (N - 1) / 2.0));
    }

    std::vector<double> blackman_window = blackman(N);
    for (int i = 0; i < N; ++i) {
        hlpf[i] *= blackman_window[i];
    }

    double hlpf_sum = std::accumulate(hlpf.begin(), hlpf.end(), 0.0);
    
    for (int i = 0; i < N; ++i) {
        hlpf[i] /= hlpf_sum;
    }

    // Compute the high-pass filter
    std::vector<double> hhpf(N);
    for (int i = 0; i < N; ++i) {
        hhpf[i] = sinc(2 * fL * (n[i] - (N - 1) / 2.0));
    }
    for (int i = 0; i < N; ++i) {
        hhpf[i] *= blackman_window[i];
    }
    double hhpf_sum = std::accumulate(hhpf.begin(), hhpf.end(), 0.0);
    for (int i = 0; i < N; ++i) {
        hhpf[i] /= hhpf_sum;
        hhpf[i] = -hhpf[i];
    }
    hhpf[(N - 1) / 2] += 1.0;

    // Convolve the two filters
    std::vector<double> h = convolve(hlpf, hhpf);

    // Output the result
    std::cout << "Filter coefficients (h):\n";
    for (const auto& coeff : h) {
        std::cout << coeff << " ";
    }
    std::cout << "\n";

    return 0;
}
