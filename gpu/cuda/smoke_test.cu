#include <cuda_runtime.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

__global__ void DeterministicTransform(const std::uint32_t* input, std::uint32_t* output,
                                       std::size_t count) {
  const std::size_t index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (index < count) {
    output[index] = input[index] * 17U + static_cast<std::uint32_t>(index * index + 3U);
  }
}

[[nodiscard]] bool Check(cudaError_t status, const char* operation) {
  if (status == cudaSuccess) {
    return true;
  }
  std::cerr << operation << " failed: " << cudaGetErrorString(status) << '\n';
  return false;
}

}  // namespace

int main() {
  int device_count = 0;
  if (!Check(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount") || device_count < 1) {
    std::cerr << "No CUDA device is available\n";
    return 1;
  }

  cudaDeviceProp properties{};
  if (!Check(cudaGetDeviceProperties(&properties, 0), "cudaGetDeviceProperties")) {
    return 1;
  }
  if (!Check(cudaSetDevice(0), "cudaSetDevice")) {
    return 1;
  }

  constexpr std::array<std::uint32_t, 8> kInput = {0U, 1U, 2U, 3U, 5U, 8U, 13U, 21U};
  std::array<std::uint32_t, kInput.size()> expected{};
  for (std::size_t index = 0; index < kInput.size(); ++index) {
    expected[index] = kInput[index] * 17U + static_cast<std::uint32_t>(index * index + 3U);
  }

  std::uint32_t* device_input = nullptr;
  std::uint32_t* device_output = nullptr;
  const std::size_t bytes = kInput.size() * sizeof(std::uint32_t);
  if (!Check(cudaMalloc(&device_input, bytes), "cudaMalloc(input)") ||
      !Check(cudaMalloc(&device_output, bytes), "cudaMalloc(output)")) {
    cudaFree(device_input);
    cudaFree(device_output);
    return 1;
  }

  bool okay = Check(cudaMemcpy(device_input, kInput.data(), bytes, cudaMemcpyHostToDevice),
                    "cudaMemcpy(input)");
  if (okay) {
    DeterministicTransform<<<1, 32>>>(device_input, device_output, kInput.size());
    okay = Check(cudaGetLastError(), "DeterministicTransform launch") &&
           Check(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
  }
  std::array<std::uint32_t, kInput.size()> actual{};
  if (okay) {
    okay = Check(cudaMemcpy(actual.data(), device_output, bytes, cudaMemcpyDeviceToHost),
                 "cudaMemcpy(output)");
  }
  cudaFree(device_input);
  cudaFree(device_output);

  if (!okay || actual != expected) {
    std::cerr << "CUDA smoke result disagrees with deterministic CPU oracle\n";
    return 1;
  }

  int runtime_version = 0;
  int driver_version = 0;
  if (!Check(cudaRuntimeGetVersion(&runtime_version), "cudaRuntimeGetVersion") ||
      !Check(cudaDriverGetVersion(&driver_version), "cudaDriverGetVersion")) {
    return 1;
  }
  std::cout << "backend=cuda device=\"" << properties.name
            << "\" compute_capability=" << properties.major << '.' << properties.minor
            << " runtime=" << runtime_version << " driver=" << driver_version
            << " global_memory_bytes=" << properties.totalGlobalMem
            << " deterministic_oracle=pass\n";
  return 0;
}
