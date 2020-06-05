#include "cinn/hlir/instruction/lower.h"

#include <glog/raw_logging.h>
#include <gtest/gtest.h>

#include <utility>

#include "cinn/backends/codegen_c.h"
#include "cinn/hlir/instruction/instruction_util.h"
#include "cinn/hlir/instruction/optimizer.h"

namespace cinn {
namespace hlir {
namespace instruction {
using cinn::Expr;

std::unique_ptr<Computation> create_elementwise_add(Context* context, cinn::Var N, const std::string& name) {
  Computation::Builder builder(context, name);
  {
    ParameterConfig parameter_config = {Float(32)};
    auto x  = builder.AddInstruction(Instruction::CreateParameter(0, Shape({N, 30, 40}), "x", parameter_config));
    auto w  = builder.AddInstruction(Instruction::CreateParameter(1, Shape({N, 30, 40}), "w", parameter_config));
    auto w1 = builder.AddInstruction(Instruction::CreateParameter(1, Shape({N, 30, 40}), "w1", parameter_config));

    auto add  = Add(x, w);
    auto add1 = Add(add, w1);

    add->set_inlined();
  }

  return builder.Build();
}

std::unique_ptr<Computation> create_activation(Context* context,
                                               cinn::Var N,
                                               const std::string& name,
                                               InstrCode instr_code) {
  Computation::Builder builder(context, name);
  {
    ParameterConfig parameter_config = {Float(32)};
    auto x    = builder.AddInstruction(Instruction::CreateParameter(0, Shape({N, 30, 40}), "x", parameter_config));
    auto acti = builder.AddInstruction(Instruction::CreateUnary(instr_code, x));
  }

  return builder.Build();
}

TEST(Lower, computation) {
  Context context;
  cinn::Var N("N");

  auto comp0 = create_elementwise_add(&context, N, "elementwise_add");

  std::cout << "HLIR:\n" << comp0->to_debug_string() << std::endl;

  ComputationLower lower(nullptr, &context);
  Expr fn = lower(comp0.get());

  LOG(INFO) << "\n" << fn;
}

TEST(Lower, tanh) {
  Context context;
  cinn::Var N("N");

  auto comp0 = create_activation(&context, N, "elementwise_add", InstrCode::Tanh);

  std::cout << "HLIR:\n" << comp0->to_debug_string() << std::endl;

  ComputationLower lower(nullptr, &context);
  Expr fn = lower(comp0.get());
}

TEST(Lower, module) {
  Context context;
  cinn::Var N("N");

  auto comp0 = create_elementwise_add(&context, N, "elementwise_add");
  auto comp1 = create_elementwise_add(&context, N, "elementwise_add1");

  Module module("module1");

  Computation::Builder main_builder(&context, "main");
  {
    ParameterConfig parameter_config = {Float(32)};
    auto x  = main_builder.AddInstruction(Instruction::CreateParameter(0, Shape({N, 30, 40}), "x11", parameter_config));
    auto w  = main_builder.AddInstruction(Instruction::CreateParameter(1, Shape({N, 30, 40}), "w11", parameter_config));
    auto w1 = main_builder.AddInstruction(Instruction::CreateParameter(2, Shape({N, 30, 40}), "w21", parameter_config));

    std::vector<Instruction*> res;

    auto call =
        main_builder.AddInstruction(Instruction::CreateCall({x, w, w1}, {"x0"}, x->shape(), x->type(), comp0.get()));
    {
      auto tuple0 = main_builder.AddInstruction(Instruction::CreateTuple(call));
      auto arg0   = main_builder.AddInstruction(Instruction::CreateTupleGet(tuple0, 0));

      auto out2 = main_builder.AddInstruction(Instruction::CreateBinary(InstrCode::Add, arg0, w, arg0->shape()));
    }

    // auto tuple = main_builder.AddInstruction(Instruction::CreateTuple(res));
  }

  module.AddComputation(std::move(comp0));
  module.AddComputation(std::move(comp1));
  module.AddEntryComputation(main_builder.Build());

  RAW_LOG(INFO, module.to_debug_string().c_str());

  Optimizer().Run(&module);

  auto cinn_module = Lower(module);

  cinn::backends::CodeGenC codegen{cinn::Target()};
  codegen.SetInlineBuiltinCodes(false);
  auto out = codegen.Compile(cinn_module, cinn::backends::CodeGenC::OutputKind::CImpl);
  std::cerr << "C code: \n" << out << std::endl;
}

TEST(Lower, inline_test) {
  Context context;
  Var N("N");

  Computation::Builder builder(&context, "elementwise_add");
  {
    ParameterConfig parameter_config = {Float(32)};
    auto x  = builder.AddInstruction(Instruction::CreateParameter(0, Shape({N, 30, 40}), "x", parameter_config));
    auto x1 = builder.AddInstruction(Instruction::CreateParameter(1, Shape({N, 30, 40}), "w", parameter_config));

    auto add0      = Add(x, x1);
    auto add1      = Add(add0, x1);
    auto add2      = Add(add0, add0);
    auto tanh_out  = Tanh(add2);
    auto tanh_out1 = Tanh(add1);
    auto add3      = Add(tanh_out, tanh_out1);
  }

  Module module("module0");
  module.AddComputation(builder.Build());

  Optimizer().Run(&module);

  LOG(INFO) << module.to_debug_string();

  cinn::backends::CodeGenC codegen{cinn::Target()};
  codegen.SetInlineBuiltinCodes(false);
  auto cinn_module = Lower(module);
  auto out         = codegen.Compile(cinn_module, cinn::backends::CodeGenC::OutputKind::CImpl);
  std::cerr << "C code: \n" << out << std::endl;
}

}  // namespace instruction
}  // namespace hlir
}  // namespace cinn
