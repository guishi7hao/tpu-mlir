//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// TPU-MLIR is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Backend/CV18xx/CV18xx.h"
#include "tpu_mlir/Backend/CV18xx/CV18xx_global_api.h"
#include "tpu_mlir/Dialect/Tpu/IR/TpuOps.h"
#include "tpu_mlir/Support/Module.h"




using namespace tpu_mlir::backend;

// =========================================
// GloballGenInterface
// =========================================
void tpu::TileOp::codegen_global_cv18xx(int64_t layer_id) {
  gaddr_t ga_input = module::getAddress(getInput());
  gaddr_t ga_output = module::getAddress(getOutput());
  int64_t n, c, h, w;
  auto fmt = module::isUniformQuantized(getOutput()) ? CVK_FMT_I8 : CVK_FMT_BF16;
  module::getNCHW(getInput(), n, c, h, w);
  cvi_backend_tg_tile_kernel(layer_id, ga_input, ga_output, n, c, h, w, getAxis(),
                             getTile(), fmt);
}

// =========================================
// LocalGenInterface
// =========================================

int64_t tpu::TileOp::getBufferSize_cv18xx(int64_t in_lmem_bytes,
                                          int64_t out_lmem_bytes,
                                          int64_t in_nslice, int64_t in_hslice,
                                          int64_t out_nslice,
                                          int64_t out_hslice) {
  return 0;
}

void tpu::TileOp::codegen_local_cv18xx(int64_t n_step, int64_t h_step, int64_t layer_id) {
  llvm_unreachable("Not supported now");
}
