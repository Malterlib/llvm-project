//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/FileWindows.h"

#include "lldb/Host/windows/windows.h"

#include <climits>
#include <io.h>
#include <mutex>
#include <stdio.h>

#include "lldb/Utility/Status.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ConvertUTF.h"

#include <algorithm>

using namespace lldb_private;

static HANDLE GetStandardFileHandle(int fd) {
  if (fd == STDIN_FILENO)
    return ::GetStdHandle(STD_INPUT_HANDLE);
  if (fd == STDOUT_FILENO)
    return ::GetStdHandle(STD_OUTPUT_HANDLE);
  if (fd == STDERR_FILENO)
    return ::GetStdHandle(STD_ERROR_HANDLE);
  return INVALID_HANDLE_VALUE;
}

static HANDLE GetStandardFileHandle(FILE *fh) {
  if (fh == stdin)
    return ::GetStdHandle(STD_INPUT_HANDLE);
  if (fh == stdout)
    return ::GetStdHandle(STD_OUTPUT_HANDLE);
  if (fh == stderr)
    return ::GetStdHandle(STD_ERROR_HANDLE);
  return INVALID_HANDLE_VALUE;
}

static bool IsWindowsConsoleHandle(HANDLE handle) {
  if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
    return false;

  DWORD mode = 0;
  return ::GetConsoleMode(handle, &mode) != 0;
}

static HANDLE GetWindowsConsoleHandle(int fd) {
  HANDLE handle = GetStandardFileHandle(fd);
  if (!IsWindowsConsoleHandle(handle) && File::DescriptorIsValid(fd))
    handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));

  return IsWindowsConsoleHandle(handle) ? handle : nullptr;
}

static HANDLE GetWindowsConsoleHandle(FILE *fh) {
  HANDLE handle = GetStandardFileHandle(fh);
  if (!IsWindowsConsoleHandle(handle) && fh != nullptr)
    handle = GetWindowsConsoleHandle(::_fileno(fh));

  return IsWindowsConsoleHandle(handle) ? handle : nullptr;
}

// Returns false for invalid UTF-8 or write failures so the caller falls back
// to the byte-oriented path.
static bool WriteUTF8ToWindowsConsole(HANDLE handle, const void *buf,
                                      size_t num_bytes) {
  if (!IsWindowsConsoleHandle(handle))
    return false;

  llvm::SmallVector<wchar_t, 256> wide_text;
  if (auto ec = llvm::sys::windows::UTF8ToUTF16(
          llvm::StringRef(static_cast<const char *>(buf), num_bytes),
          wide_text))
    return false;

  size_t written = 0;
  constexpr size_t max_write_size = 32767;
  while (written < wide_text.size()) {
    DWORD chars_to_write = static_cast<DWORD>(
        std::min(max_write_size, wide_text.size() - written));
    DWORD chars_written = 0;
    if (!::WriteConsoleW(handle, &wide_text[written], chars_to_write,
                         &chars_written, nullptr))
      return false;
    if (chars_written == 0)
      return false;
    written += chars_written;
  }
  return true;
}

NativeFileWindows::NativeFileWindows(FILE *fh, OpenOptions options,
                                     bool transfer_ownership)
    : NativeFileBase(fh, options, transfer_ownership),
      m_windows_console_handle(GetWindowsConsoleHandle(fh)) {}

NativeFileWindows::NativeFileWindows(int fd, OpenOptions options,
                                     bool transfer_ownership)
    : NativeFileBase(fd, options, transfer_ownership),
      m_windows_console_handle(GetWindowsConsoleHandle(fd)) {}

void NativeFileWindows::CalculateInteractiveAndTerminal() {
  const int fd = GetDescriptor();
  if (!File::DescriptorIsValid(fd)) {
    m_is_interactive = eLazyBoolNo;
    m_is_real_terminal = eLazyBoolNo;
    m_supports_colors = eLazyBoolNo;
    return;
  }
  m_is_interactive = eLazyBoolNo;
  m_is_real_terminal = eLazyBoolNo;
  if (_isatty(fd)) {
    m_is_interactive = eLazyBoolYes;
    m_is_real_terminal = eLazyBoolYes;
#if defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
    m_supports_colors = eLazyBoolYes;
#endif
  }
}

int NativeFileWindows::Fileno(FILE *fh) const { return ::_fileno(fh); }

int NativeFileWindows::Dup(int fd) const { return ::_dup(fd); }

IOObject::WaitableHandle NativeFileWindows::GetWaitableHandle() {
  return (HANDLE)_get_osfhandle(GetDescriptor());
}

Status NativeFileWindows::Close() {
  Status error = NativeFileBase::Close();
  m_windows_console_handle = nullptr;
  return error;
}

Status NativeFileWindows::Sync() {
  Status error;
  if (ValueGuard descriptor_guard = DescriptorIsValid()) {
    if (FlushFileBuffers((HANDLE)_get_osfhandle(m_descriptor)) == 0)
      error = Status::FromErrorString("unknown error");
  } else {
    error = Status::FromErrorString("invalid file handle");
  }
  return error;
}

bool NativeFileWindows::TryWriteDescriptorUnlocked(const void *buf,
                                                   size_t &num_bytes,
                                                   Status &error) {
  return WriteUTF8ToWindowsConsole(m_windows_console_handle, buf, num_bytes);
}

bool NativeFileWindows::TryWriteStreamUnlocked(const void *buf,
                                               size_t &num_bytes,
                                               Status &error) {
  return WriteUTF8ToWindowsConsole(m_windows_console_handle, buf, num_bytes);
}

size_t NativeFileWindows::PrintfVarArg(const char *format, va_list args) {
  // Format through File so console output reaches Write and WriteConsoleW.
  if (m_windows_console_handle)
    return File::PrintfVarArg(format, args);
  return NativeFileBase::PrintfVarArg(format, args);
}

Status NativeFileWindows::Read(void *buf, size_t &num_bytes, off_t &offset) {
  Status error;

  int fd = GetDescriptor();
  if (fd != kInvalidDescriptor) {
    // Win32 has no pread(); emulate it by saving the current offset, seeking,
    // reading, and restoring.
    std::lock_guard<std::mutex> guard(offset_access_mutex);
    long cur = ::lseek(m_descriptor, 0, SEEK_CUR);
    SeekFromStart(offset);
    error = NativeFileBase::Read(buf, num_bytes);
    if (!error.Fail())
      SeekFromStart(cur);
  } else {
    num_bytes = 0;
    error = Status::FromErrorString("invalid file handle");
  }
  return error;
}

Status NativeFileWindows::Write(const void *buf, size_t &num_bytes,
                                off_t &offset) {
  Status error;

  int fd = GetDescriptor();
  if (fd != kInvalidDescriptor) {
    // Win32 has no pwrite(); same trick as Read above, but the post-write
    // file position is what the caller wants reported back via `offset`.
    std::lock_guard<std::mutex> guard(offset_access_mutex);
    long cur = ::lseek(m_descriptor, 0, SEEK_CUR);
    SeekFromStart(offset);
    error = NativeFileBase::Write(buf, num_bytes);
    long after = ::lseek(m_descriptor, 0, SEEK_CUR);

    if (!error.Fail())
      SeekFromStart(cur);

    offset = after;
  } else {
    num_bytes = 0;
    error = Status::FromErrorString("invalid file handle");
  }
  return error;
}

void NativeFileWindows::OnStreamOpened() {
  if ((m_options & OpenOptionsModeMask) == eOpenOptionReadOnly)
    setvbuf(m_stream, nullptr, _IONBF, 0);
}

char NativeFileWindows::ID = 0;
