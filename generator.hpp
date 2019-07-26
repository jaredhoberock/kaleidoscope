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
#include "llvm/ExecutionEngine/ExecutionEngine.h"
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
          return *named_values_.at(node.name());
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

