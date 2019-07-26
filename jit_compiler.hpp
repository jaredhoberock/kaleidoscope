#pragma once

#include <vector>
#include <memory>
#include <algorithm>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/RTDyldMemoryManager.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/LambdaResolver.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/IR/Mangler.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>


class jit_compiler
{
  private:
    using compile_layer_t = llvm::orc::IRCompileLayer<llvm::orc::RTDyldObjectLinkingLayer, llvm::orc::SimpleCompiler>;

  public:
    using module_handle_t = compile_layer_t::ModuleHandleT;

    jit_compiler()
      : target_machine_(llvm::EngineBuilder().selectTarget()),
        data_layout_(target_machine_->createDataLayout()),
        object_layer_([]()
        {
          return std::make_shared<llvm::SectionMemoryManager>();
        }),
        compile_layer_(object_layer_, llvm::orc::SimpleCompiler(*target_machine_))
    {
      llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
    }

    llvm::JITSymbol find_symbol(const std::string& name)
    {
      return find_mangled_symbol(mangle(name));
    }

    module_handle_t add_module(std::unique_ptr<llvm::Module> m)
    {
      auto resolver = llvm::orc::createLambdaResolver(
        [&](const std::string& name)
        {
          if(auto symbol = find_mangled_symbol(name))
          {
            return symbol;
          }

          return llvm::JITSymbol(nullptr);
        },
        [](const std::string& name)
        {
          return nullptr;
        }
      );

      // move the module to the compile layer
      module_handle_t result = *compile_layer_.addModule(std::move(m), std::move(resolver));

      // note the handle
      module_handles_.push_back(result);

      // return it
      return result;
    }

    void remove_module(module_handle_t module)
    {
      // erase the module from our list
      module_handles_.erase(std::find(module_handles_.begin(), module_handles_.end(), module));

      // and the compile layer
      (void)compile_layer_.removeModule(module);
    }

  private:
    llvm::JITSymbol find_mangled_symbol(const std::string& name)
    {
      const bool exported_symbols_only = true;

      // search modules in reverse order
      for(auto handle = module_handles_.rbegin();
          handle != module_handles_.rend();
          ++handle)
      {
        if(auto symbol = compile_layer_.findSymbolIn(*handle, name, exported_symbols_only))
        {
          return symbol;
        }
      }

      // couldn't find the symbol in the JIT, look in the host process
      if(auto symbol_address = llvm::RTDyldMemoryManager::getSymbolAddressInProcess(name))
      {
        return llvm::JITSymbol(symbol_address, llvm::JITSymbolFlags::Exported);
      }

      // couldn't find the symbol anywhere
      return nullptr;
    }

    std::string mangle(const std::string& name) const
    {
      std::string result;
      llvm::raw_string_ostream mangled_name_stream(result);
      llvm::Mangler::getNameWithPrefix(mangled_name_stream, name, data_layout_);
      return result;
    }

    std::unique_ptr<llvm::TargetMachine> target_machine_;
    llvm::DataLayout data_layout_;
    llvm::orc::RTDyldObjectLinkingLayer object_layer_;

    compile_layer_t compile_layer_;
    std::vector<module_handle_t> module_handles_;
};

