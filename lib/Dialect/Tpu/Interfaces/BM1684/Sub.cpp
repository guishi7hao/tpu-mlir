//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// TPU-MLIR is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Backend/BM168x/BM1684.h"
#include "tpu_mlir/Dialect/Tpu/IR/TpuOps.h"

#include "tpu_mlir/Support/Module.h"

using namespace tpu_mlir::backend;

int v_is_int8_s(Value v) {
  auto d = BM168x::getDataType(v);
  return d == DTYPE_INT8 || d == DTYPE_UINT8;
}

void tpu::SubOp::codegen_global_bm1684() {
  int input_num = getInputs().size();
  assert(input_num == 2);
  int op_code = 1;
  auto a_addr = module::getAddress(getInputs()[0]);
  auto b_addr = module::getAddress(getInputs()[1]);
  auto o_addr = module::getAddress(getOutput());
  int a_shape[MAX_SHAPE_DIMS], b_shape[MAX_SHAPE_DIMS];
  auto a_dims = module::getShape(getInputs()[0]).size();
  auto b_dims = module::getShape(getInputs()[1]).size();
  auto out_dims = module::getShape(getOutput()).size();
  module::getGlobalShape(getInputs()[0], a_shape);
  module::getGlobalShape(getInputs()[1], b_shape);
  if (false == module::isUniformQuantized(getOutput())) {
    auto dtype = BM1684::getDataType(getOutput());
    int src_int32 = dtype == DTYPE_FP32 ? 0 : 1;
    auto gdma_format = BM1684::GDMA_VALUE_FORMAT_FLOAT32;
    auto buffer_size = BM1684::instance().dl_get_broadcast_binary_buffer_size(
        (uint32_t *)a_shape, a_dims, (uint32_t *)b_shape, b_dims,
        sizeof(float));
    if (buffer_size) {
      llvm_unreachable("Need Create Global Buffer");
    }
    BM1684::instance().dl_nodechip_broadcast_binary_full(
        a_addr, (uint32_t *)a_shape, a_dims, b_addr, (uint32_t *)b_shape,
        b_dims, o_addr, 0 /*buffer_addr, special case may use*/, op_code,
        getDoRelu(), getReluLimit().convertToDouble(), gdma_format,
        (CMD_ID_NODE *)BM1684::instance().cmdid_node, src_int32);
  } else {
    int sign[3] = {0};
    int is_int8[3] = {0};
    for (int i = 0; i < input_num; ++i) {
      is_int8[i] = v_is_int8_s(getInputs()[i]);
      sign[i] = module::isSign(getInputs()[i]) ? 1 : 0;
    }
    is_int8[2] = v_is_int8_s(getOutput());
    sign[2] = module::isSign(getOutput()) ? 1 : 0;
    auto multiplier_v = module::getI64Array(getMultipliers(), input_num, 1);
    auto rshift_v = module::getI64Array(getRshifts(), input_num, 0);
    BM1684::instance().dl_nodechip_broadcast_binary_fix8b_forward_parallel(
        a_addr, b_addr, o_addr, a_shape, b_shape, out_dims,
        module::isWeight(getInputs()[0]), module::isWeight(getInputs()[1]),
        op_code, (int)multiplier_v->at(0), (int)multiplier_v->at(1),
        (uint8_t)rshift_v->at(0), (uint8_t)rshift_v->at(1), is_int8, sign,
        getDoRelu() ? 1 : 0, (CMD_ID_NODE *)BM1684::instance().cmdid_node);
  }
}

int64_t tpu::SubOp::getBufferSize_bm1684(int64_t in_lmem_bytes,
                                         int64_t out_lmem_bytes,
                                         int64_t in_nslice, int64_t in_hslice,
                                         int64_t out_nslice,
                                         int64_t out_hslice) {
  int64_t buffer_size = 0;
  int input_num = getInputs().size();
  auto dtype_A = BM168x::getDataType(getInputs()[0]);
  auto dtype_O = BM168x::getDataType(getOutput());
  auto multiplier_v = module::getI64Array(getMultipliers(), input_num, 1);
  auto rshift_v = module::getI64Array(getRshifts(), input_num, 0);
  if (dtype_A == DTYPE_INT8 || dtype_A == DTYPE_UINT8) {
    if ((int)multiplier_v->at(0) != 1 || (uint8_t)rshift_v->at(0) != 0) {
      buffer_size = in_lmem_bytes * 2;
    }
  } else if ((BM168x::getFmtBytes(dtype_A) > BM168x::getFmtBytes(dtype_O)) &&
             (module::isSign(getInputs()[0]) ||
              module::isSign(getInputs()[1])) &&
             (!module::isSign(getOutput()))) {
    buffer_size = in_lmem_bytes;
  }
  return buffer_size;
}

void tpu::SubOp::codegen_local_bm1684(int64_t n_step, int64_t h_step,
                                      local_sec_info_t &sec_info) {
  int64_t n, c, h, w;
  module::getNCHW(getOutput(), n, c, h, w);
  auto out_gi = getGroupInfo(n_step, h_step);
  int num_inputs = getInputs().size();
  llvm::SmallVector<int, 8> input_addrs;
  int op_code = 1;
  for (int i = 0; i < num_inputs; i++) {
    auto in = getInputs()[i];
    auto in_ginfo = LocalGenInterface::getGroupInfo(in);
    input_addrs.push_back(in_ginfo.out_addr);
  }
  auto in0_g_info =
      LocalGenInterface::getGroupInfo(getInputs()[0], n_step, h_step);
  int64_t n0, c0, h0, w0;
  module::getNCHW(getInputs()[0], n0, c0, h0, w0);
  int b0_shape[MAX_SHAPE_DIMS] = {(int)in0_g_info.n_slice, (int)c0,
                                  (int)in0_g_info.h_slice, (int)w0};
  auto in1_g_info =
      LocalGenInterface::getGroupInfo(getInputs()[1], n_step, h_step);
  int64_t n1, c1, h1, w1;
  module::getNCHW(getInputs()[1], n1, c1, h1, w1);
  int b1_shape[MAX_SHAPE_DIMS] = {(int)in1_g_info.n_slice, (int)c1,
                                  (int)in1_g_info.h_slice, (int)w1};
  auto multiplier_v = module::getI64Array(getMultipliers(), num_inputs, 1);
  auto rshift_v = module::getI64Array(getRshifts(), num_inputs, 0);
  if (module::isUniformQuantized(getOutput())) {
    int sign[3] = {0};
    int is_int8[3] = {0};
    for (int i = 0; i < num_inputs; ++i) {
      is_int8[i] = v_is_int8_s(getInputs()[i]);
      sign[i] = module::isSign(getInputs()[i]) ? 1 : 0;
    }
    is_int8[2] = v_is_int8_s(getOutput());
    sign[2] = module::isSign(getOutput()) ? 1 : 0;
    BM1684::instance().dl_nodechip_broadcast_binary_fix8b_forward_local(
        input_addrs[0], input_addrs[1], out_gi.out_addr, out_gi.buffer_addr,
        b0_shape, b1_shape, module::getShape(getInputs()[0]).size(),
        module::isWeight(getInputs()[0]), module::isWeight(getInputs()[1]),
        op_code, (int)multiplier_v->at(0), (int)multiplier_v->at(1),
        (uint8_t)rshift_v->at(0), (uint8_t)rshift_v->at(1), is_int8, sign,
        getDoRelu(), BM1684::instance().bdc_node);
  } else {
    int b0_stride[4] = {0};
    int b1_stride[4] = {0};
    int top_stride[4] = {0};
    int top_shape[MAX_SHAPE_DIMS] = {0};
    module::getLocalShape(getOutput(), n_step, h_step, top_shape);
    module::get128BtyeAlignedStrideForNBit(b0_stride, b0_shape, BM1684::NPU_NUM,
                                           32);
    module::get128BtyeAlignedStrideForNBit(b1_stride, b1_shape, BM1684::NPU_NUM,
                                           32);
    module::get128BtyeAlignedStrideForNBit(top_stride, top_shape,
                                           BM1684::NPU_NUM, 32);
    for (int i = 0; i < 4; i++) {
      if (b0_shape[i] != b1_shape[i]) {
        if (b0_shape[i] == 1)
          b0_stride[i] = 0;
        if (b1_shape[i] == 1)
          b1_stride[i] = 0;
      }
    }
    BM1684::instance().dl_nodechip_broadcast_binary_local(
        input_addrs[0], b0_shape, b0_stride, input_addrs[1], b1_shape,
        b1_stride, out_gi.out_addr, top_stride, op_code, getDoRelu(),
        getReluLimit().convertToDouble(),
        b0_shape[1] > b1_shape[1] ? input_addrs[1] : input_addrs[0],
        BM1684::instance().bdc_node);
  }
}
