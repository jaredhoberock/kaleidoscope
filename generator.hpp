#pragma once

#include <functional>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include "syntax.hpp"

class generator
{
  public:
    generator()
      : context_(),
        builder_(context_),
        module_(make_module(context_)),
        function_pass_manager_(make_function_pass_manager(*module_))
    {}

    std::unique_ptr<llvm::Module> release_module()
    {
      // create a new module
      std::unique_ptr<llvm::Module> old_module = make_module(context_);
      std::swap(old_module, module_);

      // make a function pass manager for the new module
      function_pass_manager_ = make_function_pass_manager(*module_);

      // copy function declarations from the old module into the new
      // so that we can call functions in other modules from the new one
      for(auto& f : old_module->functions())
      {
        // get the function's parameter names
        std::vector<std::string> parameter_names;
        for(auto& arg : f.args())
        {
          parameter_names.push_back(arg.getName());
        }

        // create a function prototype
        function_prototype prototype(f.getName(), parameter_names);

        // visit the prototype to declare these functions in the new module
        visitor()(prototype);
      }

      // return the old module
      return old_module;
    }

    class visitor_type
    {
      public:
        visitor_type(const visitor_type&) = default;

        llvm::Value& operator()(const number& node) const
        {
          return *llvm::ConstantFP::get(context_, llvm::APFloat(node.value()));
        }

        llvm::Value& operator()(const variable& node) const
        {
          if(named_values_.count(node.name()) == 0)
          {
            throw std::runtime_error(std::string("No variable named '") + node.name() + "'");
          }

          return *named_values_[node.name()];
        }

        llvm::Value& operator()(const binary_operation& node) const
        {
          using namespace llvm;

          // visit both sides of the operation
          Value& lhs = visit<Value&>(*this, node.lhs());
          Value& rhs = visit<Value&>(*this, node.rhs());

          // emit the appropriate instruction
          Value* result = nullptr;
          switch(node.op())
          {
            case '+':
            {
              result = builder_.CreateFAdd(&lhs, &rhs, "addtmp");
              break;
            }

            case '-':
            {
              result = builder_.CreateFSub(&lhs, &rhs, "subtmp");
              break;
            }

            case '*':
            {
              result = builder_.CreateFMul(&lhs, &rhs, "multmp");
              break;
            }

            case '<':
            {
              // create the result of the comparison
              Value* compare_result = builder_.CreateFCmpULT(&lhs, &rhs, "cmptmp");

              // convert the unsigned integer result to floating point
              result = builder_.CreateUIToFP(compare_result, Type::getDoubleTy(context_), "booltmp");
              break;
            }

            default:
            {
              throw std::runtime_error("Invalid binary operator");
            }
          }

          return *result;
        }

        llvm::Value& operator()(const if_expression& node) const
        {
          using namespace llvm;

          // visit the if's condition
          Value& condition = visit<Value&>(*this, node.condition());

          // convert the condition into a bool by comparing non-equal to 0.0
          Value* bool_condition = builder_.CreateFCmpONE(&condition, ConstantFP::get(context_, APFloat(0.0)), "ifcond");

          // get the current function being built
          Function* current_function = builder_.GetInsertBlock()->getParent();

          // create blocks for the then and else cases and the continuation
          BasicBlock* then_block = BasicBlock::Create(context_, "then", current_function);
          BasicBlock* else_block = BasicBlock::Create(context_, "else", current_function);
          BasicBlock* continuation_block = BasicBlock::Create(context_, "ifcont", current_function);

          // create a conditional branch
          builder_.CreateCondBr(bool_condition, then_block, else_block);

          // emit then value into then_block
          builder_.SetInsertPoint(then_block);
          Value& then_value = visit<Value&>(*this, node.then_expression());
          // branch to the continuation
          builder_.CreateBr(continuation_block);
          then_block = builder_.GetInsertBlock();
          
          // emit else block
          builder_.SetInsertPoint(else_block);
          Value& else_value = visit<Value&>(*this, node.else_expression());
          // branch to the continuation
          builder_.CreateBr(continuation_block);
          else_block = builder_.GetInsertBlock();

          // emit merge block
          builder_.SetInsertPoint(continuation_block);
          PHINode* phi_node = builder_.CreatePHI(Type::getDoubleTy(context_), 2, "iftmp");
          phi_node->addIncoming(&then_value, then_block);
          phi_node->addIncoming(&else_value, else_block);
          return *phi_node;
        }

        llvm::Value& operator()(const for_expression& node) const
        {
          using namespace llvm;

          // visit the begin expression first
          Value& begin_value = visit<Value&>(*this, node.begin());

          // create a basic block for the loop body,
          // after the current function's current block
          BasicBlock* pre_loop_block = builder_.GetInsertBlock();
          Function* current_function = pre_loop_block->getParent();
          BasicBlock* loop_body_block = BasicBlock::Create(context_, "loop", current_function);

          // insert an explicit branch from the pre_loop_blocko the loop_body_block
          builder_.CreateBr(loop_body_block);

          // begin inserting IR into the loop body
          builder_.SetInsertPoint(loop_body_block);

          // create the loop variable, which takes its value from a phi node
          PHINode& loop_variable = *builder_.CreatePHI(Type::getDoubleTy(context_), 2, node.loop_variable_name());
          loop_variable.addIncoming(&begin_value, pre_loop_block);

          // shadow any variable in the outer scope with the same name as the loop variable
          Value* shadowed_variable = nullptr;
          if(named_values_.count(node.loop_variable_name()))
          {
            shadowed_variable = named_values_[node.loop_variable_name()];
          }

          // map the loop variable name to its value
          named_values_[node.loop_variable_name()] = &loop_variable;

          // generate code for the loop body
          visit<Value&>(*this, node.body());

          // generate code for the loop step
          Value& step_value = node.step()
            ? visit<Value&>(*this, *node.step())
            : *ConstantFP::get(context_, APFloat(1.0))
          ;

          // add the step to the loop variable to create its next value
          Value& next_value_of_loop_variable = *builder_.CreateFAdd(&loop_variable, &step_value, "nextvar");

          // generate the end value
          Value& end_value = visit<Value&>(*this, node.end());

          // convert the end value to a boolean
          Value& end_condition = *builder_.CreateFCmpONE(&end_value, ConstantFP::get(context_, APFloat(0.0)), "loopcond");

          // note the block which ends the loop
          BasicBlock* loop_end_block = builder_.GetInsertBlock();

          // hook up the loop variable node to the next iteration's value
          loop_variable.addIncoming(&next_value_of_loop_variable, loop_end_block);

          // create the block following the loop body
          BasicBlock* post_loop_block = BasicBlock::Create(context_, "postloop", current_function);

          // create the branch at the end of the loop
          builder_.CreateCondBr(&end_condition, loop_body_block, post_loop_block);

          // point the builder at the block following the loop
          builder_.SetInsertPoint(post_loop_block);

          // restore the shadowed variable
          if(shadowed_variable)
          {
            named_values_[node.loop_variable_name()] = shadowed_variable;
          }

          // loop expressions always return 0.0
          return *ConstantFP::get(context_, APFloat(0.0));
        }

        llvm::Function& operator()(const function_prototype& node) const
        {
          using namespace llvm;

          // create the function's type
          std::vector<Type*> parameter_types(node.parameters().size(), Type::getDoubleTy(context_));
          FunctionType* function_type = FunctionType::get(Type::getDoubleTy(context_), parameter_types, false);

          // create the function
          Function& result = *Function::Create(function_type, Function::ExternalLinkage, node.name(), &module_);

          // name the function's arguments
          int i = 0;
          for(auto& arg : result.args())
          {
            arg.setName(node.parameters()[i]);
            ++i;
          }

          return result;
        }

        llvm::Value& operator()(const call& node) const
        {
          using namespace llvm;

          // look up the callee
          Function* callee = module_.getFunction(node.callee_name());
          if(!callee)
          {
            throw std::runtime_error("Could not find function");
          }

          // check the call's arguments and type
          if(callee->arg_size() != node.arguments().size())
          {
            throw std::runtime_error("Incorrect number of arguments");
          }

          // visit each argument
          std::vector<Value*> arguments;
          for(const auto& arg_expression : node.arguments())
          {
            arguments.emplace_back(&visit<Value&>(*this, arg_expression));
          }

          // create the call
          return *builder_.CreateCall(callee, arguments, "calltmp");
        }

        llvm::Function& operator()(const function& node) const
        {
          using namespace llvm;

          // check whether this function has already been declared
          llvm::Function* result = module_.getFunction(node.prototype().name());

          if(!result)
          {
            // visit its prototype
            result = &operator()(node.prototype());
          }
          else if(!result->empty())
          {
            // the function has already been defined
            throw std::runtime_error("Function cannot be redefined");
          }

          // create a new basic block as the insert point
          builder_.SetInsertPoint(BasicBlock::Create(context_, "entry", result));

          // insert the function arguments into the named values map
          for(auto& arg : result->args())
          {
            named_values_[arg.getName()] = &arg;
          }

          // visit the function body
          try
          {
            builder_.CreateRet(&visit<Value&>(*this, node.body()));
          }
          catch(...)
          {
            // problem visiting the body, so erase the function
            result->eraseFromParent();

            // rethrow the exception
            throw;
          }

          // validate the generated code
          verifyFunction(*result);

          // optimize
          function_pass_manager_.run(*result);

          // clear the named values
          // XXX maybe this map should only exist when visiting a function
          named_values_.clear();

          return *result;
        }

        void operator()(const program& node) const
        {
          // visit each of the program's statements
          for(const auto& statement : node.statements())
          {
            ::visit<llvm::Value&>(*this, statement);
          }
        }

        template<class T>
        llvm::Value& operator()(const T&) const
        {
          throw std::runtime_error("Unhandled node type.");
        }

      private:
        friend generator;

        visitor_type(llvm::LLVMContext& ctx,
                     llvm::IRBuilder<>& builder,
                     llvm::Module& module,
                     llvm::legacy::FunctionPassManager& function_pass_manager,
                     std::map<std::string, llvm::Value*>& named_values)
          : context_(ctx),
            builder_(builder),
            module_(module),
            function_pass_manager_(function_pass_manager),
            named_values_(named_values)
        {}

        llvm::LLVMContext& context_;
        llvm::IRBuilder<>& builder_;
        llvm::Module& module_;
        llvm::legacy::FunctionPassManager& function_pass_manager_;
        std::map<std::string, llvm::Value*>& named_values_;
    };

    visitor_type visitor()
    {
      return visitor_type{context_, builder_, *module_, *function_pass_manager_, named_values_};
    }

    const llvm::Module& module() const
    {
      return *module_;
    }

  private:
    static std::unique_ptr<llvm::Module> make_module(llvm::LLVMContext& ctx)
    {
      std::unique_ptr<llvm::Module> result = std::make_unique<llvm::Module>("my jit", ctx);
      result->setDataLayout(llvm::EngineBuilder().selectTarget()->createDataLayout());

      return result;
    }

    static std::unique_ptr<llvm::legacy::FunctionPassManager> make_function_pass_manager(llvm::Module& module)
    {
      auto result = std::make_unique<llvm::legacy::FunctionPassManager>(&module);

      // simple "peephole" optimizations and bit-twiddling optimizations
      result->add(llvm::createInstructionCombiningPass());

      // reassociate expressions
      result->add(llvm::createReassociatePass());

      // eliminate common subexpressions
      result->add(llvm::createGVNPass());

      // simplify control flow graph
      result->add(llvm::createCFGSimplificationPass());

      // now initialize
      result->doInitialization();

      return result;
    }

    llvm::LLVMContext context_;
    llvm::IRBuilder<> builder_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::legacy::FunctionPassManager> function_pass_manager_;
    std::map<std::string, llvm::Value*> named_values_;
};

