//===-- ThreadMachCore.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <optional>
#include <string>
#include <vector>

#include "RegisterContextUnifiedCore.h"
#include "ThreadMachCore.h"

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/SafeMachO.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/AppleArm64ExceptionClass.h"
#include "lldb/Target/DynamicRegisterInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Unwind.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StructuredData.h"

#include "llvm/ADT/STLExtras.h"

#include "ProcessMachCore.h"
//#include "RegisterContextKDP_arm.h"
//#include "RegisterContextKDP_i386.h"
//#include "RegisterContextKDP_x86_64.h"

using namespace lldb;
using namespace lldb_private;

namespace {

struct LibpthreadOffsets {
  uint16_t plo_version = UINT16_MAX;
  uint16_t plo_pthread_tsd_base_offset = UINT16_MAX;
  uint16_t plo_pthread_tsd_base_address_offset = UINT16_MAX;
  uint16_t plo_pthread_tsd_entry_size = UINT16_MAX;

  bool IsValid() const {
    return plo_version != UINT16_MAX &&
           (plo_pthread_tsd_base_offset != UINT16_MAX ||
            plo_pthread_tsd_base_address_offset != UINT16_MAX) &&
           (plo_pthread_tsd_entry_size == 4 ||
            plo_pthread_tsd_entry_size == 8);
  }
};

constexpr addr_t kPthreadNextOffset = 0x10;
// Darwin arm64 pthread fields used to associate LC_THREAD register state with
// libpthread's saved per-thread state in a core file.
constexpr addr_t kPthreadStackAddressOffset = 0xb0;
constexpr addr_t kPthreadStackBottomOffset = 0xb8;
constexpr uint32_t kMaxPthreadListCount = 1024;

bool IsValidAddress(addr_t addr) {
  return addr != 0 && addr != LLDB_INVALID_ADDRESS;
}

ModuleSP FindLibpthreadModule(Target &target) {
  ModuleSpec module_spec(FileSpec("libsystem_pthread.dylib"));
  return target.GetImages().FindFirstModule(module_spec);
}

addr_t GetDataSymbolLoadAddress(Target &target, Module &module,
                                llvm::StringRef name) {
  const Symbol *symbol =
      module.FindFirstSymbolWithNameAndType(ConstString(name), eSymbolTypeData);
  if (!symbol)
    return LLDB_INVALID_ADDRESS;

  return symbol->GetLoadAddress(&target);
}

std::optional<LibpthreadOffsets>
ReadLibpthreadOffsets(Process &process, Target &target, Module &module) {
  addr_t offsets_addr =
      GetDataSymbolLoadAddress(target, module, "pthread_layout_offsets");
  if (!IsValidAddress(offsets_addr))
    return std::nullopt;

  uint8_t memory_buffer[sizeof(LibpthreadOffsets)];
  Status error;
  if (process.ReadMemory(offsets_addr, memory_buffer, sizeof(memory_buffer),
                         error) != sizeof(memory_buffer))
    return std::nullopt;

  DataExtractor data(memory_buffer, sizeof(memory_buffer),
                     process.GetByteOrder(), process.GetAddressByteSize());
  lldb::offset_t data_offset = 0;

  LibpthreadOffsets offsets;
  data.GetU16(&data_offset, &offsets.plo_version,
              sizeof(LibpthreadOffsets) / sizeof(uint16_t));
  if (!offsets.IsValid())
    return std::nullopt;

  return offsets;
}

addr_t ReadPointer(Process &process, addr_t address) {
  if (!IsValidAddress(address))
    return LLDB_INVALID_ADDRESS;

  Status error;
  addr_t value = process.ReadPointerFromMemory(address, error);
  if (error.Fail() || !IsValidAddress(value))
    return LLDB_INVALID_ADDRESS;

  return value;
}

addr_t ReadUnsigned(Process &process, addr_t address, size_t byte_size) {
  if (!IsValidAddress(address))
    return LLDB_INVALID_ADDRESS;

  Status error;
  addr_t value = process.ReadUnsignedIntegerFromMemory(
      address, byte_size, LLDB_INVALID_ADDRESS, error);
  if (error.Fail() || !IsValidAddress(value))
    return LLDB_INVALID_ADDRESS;

  return value;
}

addr_t GetTSDAddressForPthread(Process &process, addr_t pthread,
                               const LibpthreadOffsets &offsets) {
  if (!IsValidAddress(pthread))
    return LLDB_INVALID_ADDRESS;

  if (offsets.plo_pthread_tsd_base_offset != 0 &&
      offsets.plo_pthread_tsd_base_offset != UINT16_MAX)
    return pthread + offsets.plo_pthread_tsd_base_offset;

  if (offsets.plo_pthread_tsd_base_address_offset == UINT16_MAX)
    return LLDB_INVALID_ADDRESS;

  return ReadUnsigned(process,
                      pthread + offsets.plo_pthread_tsd_base_address_offset,
                      offsets.plo_pthread_tsd_entry_size);
}

bool StackPointerIsInPthreadStack(Process &process, addr_t pthread, addr_t sp) {
  addr_t stack_addr =
      ReadPointer(process, pthread + kPthreadStackAddressOffset);
  addr_t stack_bottom =
      ReadPointer(process, pthread + kPthreadStackBottomOffset);
  if (!IsValidAddress(stack_addr) || !IsValidAddress(stack_bottom))
    return false;

  return stack_bottom <= sp && sp < stack_addr;
}

addr_t GetDarwinThreadPointerFromPthreadList(Process &process, addr_t sp) {
  if (!IsValidAddress(sp))
    return LLDB_INVALID_ADDRESS;

  Target &target = process.GetTarget();
  ModuleSP libpthread_module_sp = FindLibpthreadModule(target);
  if (!libpthread_module_sp)
    return LLDB_INVALID_ADDRESS;

  std::optional<LibpthreadOffsets> offsets =
      ReadLibpthreadOffsets(process, target, *libpthread_module_sp);
  if (!offsets)
    return LLDB_INVALID_ADDRESS;

  addr_t pthread_head_addr =
      GetDataSymbolLoadAddress(target, *libpthread_module_sp, "__pthread_head");
  if (!IsValidAddress(pthread_head_addr))
    return LLDB_INVALID_ADDRESS;

  uint32_t pthread_count = kMaxPthreadListCount;
  addr_t pthread_count_addr =
      GetDataSymbolLoadAddress(target, *libpthread_module_sp, "_pthread_count");
  if (IsValidAddress(pthread_count_addr)) {
    Status error;
    uint64_t count = process.ReadUnsignedIntegerFromMemory(
        pthread_count_addr, 4, 0, error);
    if (error.Success() && count > 0)
      pthread_count = std::min<uint64_t>(count, kMaxPthreadListCount);
  }

  addr_t pthread = ReadPointer(process, pthread_head_addr);
  std::vector<addr_t> visited;
  visited.reserve(std::min<uint32_t>(pthread_count, 32));

  for (uint32_t i = 0; i < pthread_count && IsValidAddress(pthread); ++i) {
    if (llvm::is_contained(visited, pthread))
      break;
    visited.push_back(pthread);

    if (StackPointerIsInPthreadStack(process, pthread, sp)) {
      addr_t tsd_addr = GetTSDAddressForPthread(process, pthread, *offsets);
      if (IsValidAddress(tsd_addr))
        return tsd_addr;
    }

    pthread = ReadPointer(process, pthread + kPthreadNextOffset);
  }

  return LLDB_INVALID_ADDRESS;
}

} // namespace

// Thread Registers

ThreadMachCore::ThreadMachCore(Process &process, lldb::tid_t tid,
                               uint32_t objfile_lc_thread_idx)
    : Thread(process, tid), m_thread_name(), m_dispatch_queue_name(),
      m_thread_dispatch_qaddr(LLDB_INVALID_ADDRESS), m_thread_reg_ctx_sp(),
      m_objfile_lc_thread_idx(objfile_lc_thread_idx) {}

ThreadMachCore::~ThreadMachCore() { DestroyThread(); }

const char *ThreadMachCore::GetName() {
  if (m_thread_name.empty())
    return nullptr;
  return m_thread_name.c_str();
}

void ThreadMachCore::RefreshStateAfterStop() {
  // Invalidate all registers in our register context. We don't set "force" to
  // true because the stop reply packet might have had some register values
  // that were expedited and these will already be copied into the register
  // context by the time this function gets called. The KDPRegisterContext
  // class has been made smart enough to detect when it needs to invalidate
  // which registers are valid by putting hooks in the register read and
  // register supply functions where they check the process stop ID and do the
  // right thing.
  const bool force = false;
  GetRegisterContext()->InvalidateIfNeeded(force);
}

bool ThreadMachCore::ThreadIDIsValid(lldb::tid_t thread) { return thread != 0; }

lldb::RegisterContextSP ThreadMachCore::GetRegisterContext() {
  if (!m_reg_context_sp)
    m_reg_context_sp = CreateRegisterContextForFrame(nullptr);
  return m_reg_context_sp;
}

lldb::addr_t ThreadMachCore::GetThreadPointer() {
  addr_t thread_pointer = Thread::GetThreadPointer();
  if (thread_pointer != LLDB_INVALID_ADDRESS)
    return thread_pointer;

  ProcessSP process_sp(GetProcess());
  if (!process_sp)
    return LLDB_INVALID_ADDRESS;

  Target &target = process_sp->GetTarget();
  const ArchSpec arch_spec = target.GetArchitecture();
  const uint32_t cputype = arch_spec.GetMachOCPUType();
  if (cputype != llvm::MachO::CPU_TYPE_ARM64 &&
      cputype != llvm::MachO::CPU_TYPE_ARM64_32)
    return LLDB_INVALID_ADDRESS;

  RegisterContextSP reg_ctx_sp = GetRegisterContext();
  if (!reg_ctx_sp)
    return LLDB_INVALID_ADDRESS;

  thread_pointer = reg_ctx_sp->GetThreadPointer();
  if (thread_pointer != LLDB_INVALID_ADDRESS)
    return thread_pointer;

  return GetDarwinThreadPointerFromPthreadList(*process_sp, reg_ctx_sp->GetSP());
}

lldb::RegisterContextSP
ThreadMachCore::CreateRegisterContextForFrame(StackFrame *frame) {
  uint32_t concrete_frame_idx = 0;

  if (frame)
    concrete_frame_idx = frame->GetConcreteFrameIndex();
  if (concrete_frame_idx > 0)
    return GetUnwinder().CreateRegisterContextForFrame(frame);

  if (m_thread_reg_ctx_sp)
    return m_thread_reg_ctx_sp;

  ProcessSP process_sp(GetProcess());
  assert(process_sp);

  ObjectFile *core_objfile =
      static_cast<ProcessMachCore *>(process_sp.get())->GetCoreObjectFile();
  if (!core_objfile)
    return {};

  RegisterContextSP core_thread_regctx_sp =
      core_objfile->GetThreadContextAtIndex(m_objfile_lc_thread_idx, *this);

  if (!core_thread_regctx_sp)
    return {};

  StructuredData::ObjectSP process_md_sp =
      core_objfile->GetCorefileProcessMetadata();

  StructuredData::ObjectSP thread_md_sp;
  if (process_md_sp && process_md_sp->GetAsDictionary() &&
      process_md_sp->GetAsDictionary()->HasKey("threads")) {
    StructuredData::Array *threads = process_md_sp->GetAsDictionary()
                                         ->GetValueForKey("threads")
                                         ->GetAsArray();
    if (threads && threads->GetSize() == core_objfile->GetNumThreadContexts()) {
      StructuredData::ObjectSP thread_sp =
          threads->GetItemAtIndex(m_objfile_lc_thread_idx);
      if (thread_sp && thread_sp->GetAsDictionary())
        thread_md_sp = thread_sp;
    }
  }
  m_thread_reg_ctx_sp = std::make_shared<RegisterContextUnifiedCore>(
      *this, concrete_frame_idx, core_thread_regctx_sp, thread_md_sp);

  return m_thread_reg_ctx_sp;
}

static bool IsCrashExceptionClass(AppleArm64ExceptionClass EC) {
  switch (EC) {
  case AppleArm64ExceptionClass::ESR_EC_UNCATEGORIZED:
  case AppleArm64ExceptionClass::ESR_EC_SVC_32:
  case AppleArm64ExceptionClass::ESR_EC_SVC_64:
    // In the ARM exception model, a process takes an exception when asking the
    // kernel to service a system call. Don't treat this like a crash.
    return false;
  default:
    return true;
  }
}

bool ThreadMachCore::CalculateStopInfo() {
  ProcessSP process_sp(GetProcess());
  if (process_sp) {
    StopInfoSP stop_info;
    RegisterContextSP reg_ctx_sp = GetRegisterContext();

    if (reg_ctx_sp) {
      Target &target = process_sp->GetTarget();
      const ArchSpec arch_spec = target.GetArchitecture();
      const uint32_t cputype = arch_spec.GetMachOCPUType();

      if (cputype == llvm::MachO::CPU_TYPE_ARM64 ||
          cputype == llvm::MachO::CPU_TYPE_ARM64_32) {
        const RegisterInfo *esr_info = reg_ctx_sp->GetRegisterInfoByName("esr");
        const RegisterInfo *far_info = reg_ctx_sp->GetRegisterInfoByName("far");
        RegisterValue esr, far;
        if (reg_ctx_sp->ReadRegister(esr_info, esr) &&
            reg_ctx_sp->ReadRegister(far_info, far)) {
          const uint32_t esr_val = esr.GetAsUInt32();
          const AppleArm64ExceptionClass exception_class =
              getAppleArm64ExceptionClass(esr_val);
          if (IsCrashExceptionClass(exception_class)) {
            StreamString S;
            S.Printf("%s (fault address: 0x%" PRIx64 ")",
                     toString(exception_class), far.GetAsUInt64());
            stop_info =
                StopInfo::CreateStopReasonWithException(*this, S.GetData());
          }
        }
      }
    }

    // Set a stop reason for crashing threads only so that they get selected
    // preferentially.
    if (stop_info)
      SetStopInfo(stop_info);
    return true;
  }
  return false;
}
