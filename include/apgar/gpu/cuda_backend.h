#ifndef APGAR_GPU_CUDA_BACKEND_H_
#define APGAR_GPU_CUDA_BACKEND_H_

#include <memory>

#include "apgar/gpu/planar_router.h"

namespace apgar::gpu {

[[nodiscard]] std::unique_ptr<IPlanarRouteBackend> CreateCudaPlanarRouteBackend();

}  // namespace apgar::gpu

#endif  // APGAR_GPU_CUDA_BACKEND_H_
