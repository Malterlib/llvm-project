//===-- ThreadMinidump.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ThreadMinidump.h"

#include "NtStructures.h"
#include "ProcessMinidump.h"

#include "RegisterContextMinidump_ARM.h"
#include "RegisterContextMinidump_ARM64.h"
#include "RegisterContextMinidump_x86_32.h"
#include "RegisterContextMinidump_x86_64.h"

#include "Plugins/Process/Utility/RegisterContextLinux_i386.h"
#include "Plugins/Process/Utility/RegisterContextLinux_x86_64.h"
#include "Plugins/Process/elf-core/RegisterContextPOSIXCore_x86_64.h"
#include "Plugins/Process/elf-core/RegisterUtilities.h"

#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Target/Unwind.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "llvm/Support/Endian.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;
using namespace minidump;

namespace {

bool IsValidThreadPointer(lldb::addr_t addr) {
  return addr != 0 && addr != LLDB_INVALID_ADDRESS;
}

lldb::addr_t ReadTebPointer(Process &process, lldb::addr_t address,
                            uint32_t pointer_size) {
  if (pointer_size != 4 && pointer_size != 8)
    return LLDB_INVALID_ADDRESS;

  uint8_t data[8] = {};
  Status error;
  size_t bytes = process.ReadMemory(address, data, pointer_size, error);
  if (error.Fail() || bytes != pointer_size)
    return LLDB_INVALID_ADDRESS;

  if (pointer_size == 4)
    return llvm::support::endian::read32le(data);
  return llvm::support::endian::read64le(data);
}

bool ReadTebStackBounds(Process &process, lldb::addr_t thread_pointer,
                        uint32_t pointer_size,
                        lldb::addr_t &stack_base,
                        lldb::addr_t &stack_limit) {
  if (!IsValidThreadPointer(thread_pointer))
    return false;

  stack_base = ReadTebPointer(process, thread_pointer + pointer_size,
                              pointer_size);
  if (!IsValidThreadPointer(stack_base))
    return false;

  stack_limit = ReadTebPointer(process, thread_pointer + 2 * pointer_size,
                               pointer_size);
  if (!IsValidThreadPointer(stack_limit))
    return false;

  return stack_limit <= stack_base;
}

bool TebStackContainsSP(Process &process, lldb::addr_t thread_pointer,
                        uint32_t pointer_size, lldb::addr_t sp) {
  if (!IsValidThreadPointer(sp))
    return false;

  lldb::addr_t stack_base = LLDB_INVALID_ADDRESS;
  lldb::addr_t stack_limit = LLDB_INVALID_ADDRESS;
  if (!ReadTebStackBounds(process, thread_pointer, pointer_size, stack_base,
                          stack_limit))
    return false;

  return stack_limit <= sp && sp < stack_base;
}

lldb::addr_t GetWow64ThreadPointer(Process &process,
                                   lldb::addr_t native_thread_pointer) {
  if (!IsValidThreadPointer(native_thread_pointer))
    return LLDB_INVALID_ADDRESS;

  TEB64 teb = {};
  Status error;
  size_t bytes =
      process.ReadMemory(native_thread_pointer, &teb, sizeof(teb), error);
  if (error.Fail() || bytes != sizeof(teb))
    return LLDB_INVALID_ADDRESS;

  lldb::addr_t thread_pointer =
      static_cast<lldb::addr_t>(static_cast<uint64_t>(teb.reserved1[0]));
  if (!IsValidThreadPointer(thread_pointer))
    return LLDB_INVALID_ADDRESS;

  return thread_pointer;
}

} // namespace

ThreadMinidump::ThreadMinidump(Process &process, const minidump::Thread &td,
                               llvm::ArrayRef<uint8_t> gpregset_data)
    : Thread(process, td.ThreadId), m_thread_reg_ctx_sp(),
      m_gpregset_data(gpregset_data), m_thread_pointer(td.EnvironmentBlock),
      m_effective_thread_pointer(LLDB_INVALID_ADDRESS) {}

ThreadMinidump::~ThreadMinidump() = default;

void ThreadMinidump::RefreshStateAfterStop() {}

RegisterContextSP ThreadMinidump::GetRegisterContext() {
  if (!m_reg_context_sp) {
    m_reg_context_sp = CreateRegisterContextForFrame(nullptr);
  }
  return m_reg_context_sp;
}

RegisterContextSP
ThreadMinidump::CreateRegisterContextForFrame(StackFrame *frame) {
  RegisterContextSP reg_ctx_sp;
  uint32_t concrete_frame_idx = 0;

  if (frame)
    concrete_frame_idx = frame->GetConcreteFrameIndex();

  if (concrete_frame_idx == 0) {
    if (m_thread_reg_ctx_sp)
      return m_thread_reg_ctx_sp;

    ProcessMinidump *process =
        static_cast<ProcessMinidump *>(GetProcess().get());
    ArchSpec arch = process->GetArchitecture();
    RegisterInfoInterface *reg_interface = nullptr;

    // TODO write other register contexts and add them here
    switch (arch.GetMachine()) {
    case llvm::Triple::x86: {
      reg_interface = new RegisterContextLinux_i386(arch);
      lldb::DataBufferSP buf =
          ConvertMinidumpContext_x86_32(m_gpregset_data, reg_interface);
      DataExtractor gpregset(buf, lldb::eByteOrderLittle, 4);
      m_thread_reg_ctx_sp = std::make_shared<RegisterContextCorePOSIX_x86_64>(
          *this, reg_interface, gpregset,
          llvm::ArrayRef<lldb_private::CoreNote>());
      break;
    }
    case llvm::Triple::x86_64: {
      reg_interface = new RegisterContextLinux_x86_64(arch);
      lldb::DataBufferSP buf =
          ConvertMinidumpContext_x86_64(m_gpregset_data, reg_interface);
      DataExtractor gpregset(buf, lldb::eByteOrderLittle, 8);
      m_thread_reg_ctx_sp = std::make_shared<RegisterContextCorePOSIX_x86_64>(
          *this, reg_interface, gpregset,
          llvm::ArrayRef<lldb_private::CoreNote>());
      break;
    }
    case llvm::Triple::aarch64: {
      DataExtractor data(m_gpregset_data.data(), m_gpregset_data.size(),
                         lldb::eByteOrderLittle, 8);
      m_thread_reg_ctx_sp =
          std::make_shared<RegisterContextMinidump_ARM64>(*this, data);
      break;
    }
    case llvm::Triple::arm: {
      DataExtractor data(m_gpregset_data.data(), m_gpregset_data.size(),
                         lldb::eByteOrderLittle, 8);
      const bool apple = arch.GetTriple().getVendor() == llvm::Triple::Apple;
      m_thread_reg_ctx_sp =
          std::make_shared<RegisterContextMinidump_ARM>(*this, data, apple);
      break;
    }
    default:
      break;
    }

    reg_ctx_sp = m_thread_reg_ctx_sp;
  } else if (m_unwinder_up) {
    reg_ctx_sp = m_unwinder_up->CreateRegisterContextForFrame(frame);
  }

  return reg_ctx_sp;
}

lldb::addr_t ThreadMinidump::GetThreadPointer() {
  if (m_effective_thread_pointer != LLDB_INVALID_ADDRESS)
    return m_effective_thread_pointer;

  if (m_thread_pointer != 0) {
    ProcessSP process_sp = GetProcess();
    if (!process_sp)
      return Thread::GetThreadPointer();

    const bool is_windows =
        process_sp->GetTarget().GetArchitecture().GetTriple().isOSWindows();
    if (!is_windows) {
      m_effective_thread_pointer = m_thread_pointer;
      return m_effective_thread_pointer;
    }

    ProcessMinidump *process_minidump =
        static_cast<ProcessMinidump *>(process_sp.get());
    auto get_target_thread_pointer = [&](ThreadMinidump &thread) {
      lldb::addr_t thread_pointer = thread.m_thread_pointer;
      if (process_minidump->IsWow64())
        thread_pointer = GetWow64ThreadPointer(*process_sp, thread_pointer);
      return thread_pointer;
    };
    const uint32_t teb_pointer_size =
        process_minidump->IsWow64() ? 4 : process_sp->GetAddressByteSize();

    m_effective_thread_pointer = get_target_thread_pointer(*this);
    if (!IsValidThreadPointer(m_effective_thread_pointer))
      return Thread::GetThreadPointer();

    RegisterContextSP reg_ctx_sp = GetRegisterContext();
    lldb::addr_t sp =
        reg_ctx_sp ? reg_ctx_sp->GetSP(LLDB_INVALID_ADDRESS)
                   : LLDB_INVALID_ADDRESS;
    if (!IsValidThreadPointer(sp) ||
        TebStackContainsSP(*process_sp, m_effective_thread_pointer,
                           teb_pointer_size, sp))
      return m_effective_thread_pointer;

    ThreadList &thread_list = process_sp->GetThreadList();
    const uint32_t num_threads = thread_list.GetSize(false);
    for (uint32_t i = 0; i < num_threads; ++i) {
      ThreadSP thread_sp = thread_list.GetThreadAtIndex(i, false);
      if (!thread_sp)
        continue;

      ThreadMinidump *thread =
          static_cast<ThreadMinidump *>(thread_sp.get());
      lldb::addr_t candidate_thread_pointer =
          get_target_thread_pointer(*thread);
      if (candidate_thread_pointer == m_effective_thread_pointer)
        continue;

      if (TebStackContainsSP(*process_sp, candidate_thread_pointer,
                             teb_pointer_size, sp)) {
        m_effective_thread_pointer = candidate_thread_pointer;
        return m_effective_thread_pointer;
      }
    }

    return m_effective_thread_pointer;
  }

  return Thread::GetThreadPointer();
}

bool ThreadMinidump::CalculateStopInfo() { return false; }
