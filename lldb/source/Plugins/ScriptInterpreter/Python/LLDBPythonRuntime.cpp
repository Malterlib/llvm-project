//===-- LLDBPythonRuntime.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_PYTHON && LLDB_DYNAMIC_PYTHON_RUNTIME

#define LLDB_PYTHON_RUNTIME_IMPLEMENTATION
#include "lldb-python.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#if defined(Py_LIMITED_API) && (Py_LIMITED_API < 0x030a0000)
extern "C" const char *PyUnicode_AsUTF8AndSize(PyObject *, Py_ssize_t *);
#endif

namespace {

std::once_flag g_python_runtime_once;
std::string g_python_runtime_error;

constexpr int g_python_runtime_minimum_major =
    (LLDB_PYTHON_LIMITED_API_VERSION >> 24) & 0xff;
constexpr int g_python_runtime_minimum_minor =
    (LLDB_PYTHON_LIMITED_API_VERSION >> 16) & 0xff;

auto fg_ReadCommandOutput(const char *command) -> std::string {
  std::string output;
  FILE *pipe = popen(command, "r");
  if (!pipe)
    return output;

  std::array<char, 4096> buffer;
  while (fgets(buffer.data(), buffer.size(), pipe))
    output += buffer.data();

  pclose(pipe);
  return output;
}

auto fg_SplitLines(llvm::StringRef text) -> std::vector<std::string> {
  std::vector<std::string> lines;
  while (!text.empty()) {
    llvm::StringRef line;
    std::tie(line, text) = text.split('\n');
    line = line.trim();
    if (!line.empty())
      lines.push_back(line.str());
  }
  return lines;
}

auto fg_GetPythonLibraryCandidates() -> std::vector<std::string> {
  std::vector<std::string> candidates;

  if (const char *library = std::getenv("LLDB_PYTHON_LIBRARY")) {
    if (library[0])
      candidates.push_back(library);
  }

  const char *script =
      "python3 - <<'PY'\n"
      "import os\n"
      "import sysconfig\n"
      "\n"
      "libdirs = [\n"
      "    sysconfig.get_config_var('LIBDIR'),\n"
      "    sysconfig.get_config_var('LIBPL'),\n"
      "]\n"
      "names = [\n"
      "    sysconfig.get_config_var('LDLIBRARY'),\n"
      "    sysconfig.get_config_var('INSTSONAME'),\n"
      "]\n"
      "framework = sysconfig.get_config_var('PYTHONFRAMEWORK')\n"
      "framework_prefix = sysconfig.get_config_var('PYTHONFRAMEWORKPREFIX')\n"
      "framework_version = sysconfig.get_config_var('PYTHONFRAMEWORKVERSION')\n"
      "if framework and framework_prefix and framework_version:\n"
      "    path = os.path.join(framework_prefix, framework + '.framework',\n"
      "                        'Versions', framework_version, framework)\n"
      "    if os.path.exists(path):\n"
      "        print(path)\n"
      "seen = set()\n"
      "for libdir in libdirs:\n"
      "    if not libdir or libdir in seen:\n"
      "        continue\n"
      "    seen.add(libdir)\n"
      "    for name in names:\n"
      "        if name:\n"
      "            path = os.path.join(libdir, name)\n"
      "            if os.path.exists(path):\n"
      "                print(path)\n"
      "for name in names:\n"
      "    if name:\n"
      "        print(name)\n"
      "PY";

  for (std::string &candidate : fg_SplitLines(fg_ReadCommandOutput(script)))
    candidates.push_back(std::move(candidate));

#if defined(__APPLE__)
  candidates.push_back("libpython3.dylib");
  candidates.push_back("Python.framework/Python");
  candidates.push_back("Python3.framework/Python3");
#else
  candidates.push_back("libpython3.so");
#endif
  for (int minor = 99; minor >= g_python_runtime_minimum_minor; --minor) {
#if defined(__APPLE__)
    candidates.push_back(llvm::formatv("libpython3.{0}.dylib", minor).str());
#else
    candidates.push_back(llvm::formatv("libpython3.{0}.so.1.0", minor).str());
    candidates.push_back(llvm::formatv("libpython3.{0}.so.1", minor).str());
    candidates.push_back(llvm::formatv("libpython3.{0}.so", minor).str());
#endif
  }

  return candidates;
}

auto fg_ParsePythonVersion(llvm::StringRef version, int &major, int &minor)
    -> bool {
  llvm::StringRef major_string;
  llvm::StringRef rest;
  std::tie(major_string, rest) = version.split('.');

  llvm::StringRef minor_string;
  std::tie(minor_string, rest) = rest.split('.');

  return !major_string.getAsInteger(10, major) &&
         !minor_string.getAsInteger(10, minor);
}

auto fg_GetPythonRuntimeMinimumVersion() -> std::string {
  return llvm::formatv("{0}.{1}", g_python_runtime_minimum_major,
                       g_python_runtime_minimum_minor)
      .str();
}

auto fg_AcceptPythonRuntimeVersion(const char *version,
                                   llvm::StringRef source) -> bool {
  int major = 0;
  int minor = 0;
  if (!fg_ParsePythonVersion(version, major, minor) ||
      major != g_python_runtime_minimum_major ||
      minor < g_python_runtime_minimum_minor) {
    g_python_runtime_error = "Loaded Python runtime is too old: " +
                             source.str() + ". Need Python " +
                             fg_GetPythonRuntimeMinimumVersion() + " or newer.";
    return false;
  }

  return true;
}

auto fg_GetProcessLibrary() -> llvm::sys::DynamicLibrary & {
  static llvm::sys::DynamicLibrary library =
      llvm::sys::DynamicLibrary::getPermanentLibrary(nullptr);
  return library;
}

auto fg_LoadPythonRuntime() -> void {
  using FPyGetVersion = const char *(*)();
  llvm::sys::DynamicLibrary &process_library = fg_GetProcessLibrary();
  if (process_library.isValid()) {
    auto process_py_get_version = reinterpret_cast<FPyGetVersion>(
        process_library.getAddressOfSymbol("Py_GetVersion"));
    if (process_py_get_version) {
      fg_AcceptPythonRuntimeVersion(process_py_get_version(),
                                    "current process");
      return;
    }
  }

  std::string load_errors;

  for (const std::string &candidate : fg_GetPythonLibraryCandidates()) {
    std::string error;
    llvm::sys::DynamicLibrary library =
        llvm::sys::DynamicLibrary::getPermanentLibrary(candidate.c_str(), &error);
    if (!library.isValid()) {
      if (!candidate.empty() && !error.empty())
        load_errors += candidate + ": " + error + "\n";
      continue;
    }

    auto py_get_version = reinterpret_cast<FPyGetVersion>(
        library.getAddressOfSymbol("Py_GetVersion"));
    if (!py_get_version) {
      g_python_runtime_error =
          "Loaded Python runtime does not export Py_GetVersion: " + candidate;
      return;
    }

    if (!fg_AcceptPythonRuntimeVersion(py_get_version(), candidate))
      return;

    return;
  }

  g_python_runtime_error =
      "Unable to load a Python " + fg_GetPythonRuntimeMinimumVersion() +
      " or newer runtime library.\n" + load_errors;
}

auto fg_EnsurePythonRuntime() -> bool {
  std::call_once(g_python_runtime_once, fg_LoadPythonRuntime);
  return g_python_runtime_error.empty();
}

[[noreturn]] auto fg_FatalPythonRuntimeError(const char *symbol) -> void {
  std::string message = "LLDB failed to load the Python runtime";
  if (symbol && symbol[0]) {
    message += " while resolving ";
    message += symbol;
  }
  if (!g_python_runtime_error.empty()) {
    message += ":\n";
    message += g_python_runtime_error;
  }
  fputs(message.c_str(), stderr);
  fputc('\n', stderr);
  abort();
}

auto fg_ResolvePythonSymbol(const char *symbol) -> void * {
  if (!fg_EnsurePythonRuntime())
    fg_FatalPythonRuntimeError(symbol);

  void *address = nullptr;
  llvm::sys::DynamicLibrary &process_library = fg_GetProcessLibrary();
  if (process_library.isValid())
    address = process_library.getAddressOfSymbol(symbol);

  if (!address)
    address = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(symbol);

  if (!address) {
    g_python_runtime_error = "Loaded Python runtime does not export ";
    g_python_runtime_error += symbol;
    fg_FatalPythonRuntimeError(symbol);
  }
  return address;
}

template <typename T> auto fg_ResolvePythonFunction(const char *symbol) -> T {
  return reinterpret_cast<T>(fg_ResolvePythonSymbol(symbol));
}

template <typename T> auto fg_ResolvePythonData(const char *symbol) -> T * {
  return reinterpret_cast<T *>(fg_ResolvePythonSymbol(symbol));
}

template <typename T> auto fg_ResolvePythonObjectPointer(const char *symbol) -> T {
  return *fg_ResolvePythonData<T>(symbol);
}

auto fg_PyVaBuildValue(const char *format, va_list varargs) -> PyObject * {
  using FFunction = PyObject *(*)(const char *, va_list);
  static FFunction fFunction =
      fg_ResolvePythonFunction<FFunction>("Py_VaBuildValue");
  return fFunction(format, varargs);
}

auto fg_IsTuple(PyObject *object) -> bool {
  static PyTypeObject *tuple_type =
      fg_ResolvePythonData<PyTypeObject>("PyTuple_Type");
  return PyObject_TypeCheck(object, tuple_type);
}

#define DForwardPythonFunction(d_ReturnType, d_Name, d_Parameters, d_Arguments) \
  extern "C" d_ReturnType d_Name d_Parameters {                                \
    using FFunction = d_ReturnType(*) d_Parameters;                             \
    static FFunction fFunction =                                                \
        fg_ResolvePythonFunction<FFunction>(#d_Name);                           \
    return fFunction d_Arguments;                                               \
  }

#define DForwardPythonVoidFunction(d_Name, d_Parameters, d_Arguments)           \
  extern "C" void d_Name d_Parameters {                                         \
    using FFunction = void (*) d_Parameters;                                    \
    static FFunction fFunction =                                                \
        fg_ResolvePythonFunction<FFunction>(#d_Name);                           \
    fFunction d_Arguments;                                                      \
  }

#define DForwardPythonData(d_ReturnType, d_Name, d_Symbol)                     \
  extern "C" d_ReturnType *d_Name() {                                          \
    static d_ReturnType *data = fg_ResolvePythonData<d_ReturnType>(d_Symbol);   \
    return data;                                                               \
  }

#define DForwardPythonObjectPointer(d_Name, d_Symbol)                          \
  extern "C" PyObject *d_Name() {                                              \
    static PyObject *object =                                                   \
        fg_ResolvePythonObjectPointer<PyObject *>(d_Symbol);                    \
    return object;                                                             \
  }

} // namespace

extern "C" bool LLDBPythonRuntime_Initialize() {
  return fg_EnsurePythonRuntime();
}

DForwardPythonData(PyObject, LLDBPythonRuntime_Get__Py_FalseStruct,
                   "_Py_FalseStruct")
DForwardPythonData(PyObject, LLDBPythonRuntime_Get__Py_NoneStruct,
                   "_Py_NoneStruct")
DForwardPythonData(PyObject, LLDBPythonRuntime_Get__Py_NotImplementedStruct,
                   "_Py_NotImplementedStruct")
DForwardPythonData(PyObject, LLDBPythonRuntime_Get__Py_TrueStruct,
                   "_Py_TrueStruct")

DForwardPythonData(PyTypeObject, LLDBPythonRuntime_GetPyBool_Type,
                   "PyBool_Type")
DForwardPythonData(PyTypeObject, LLDBPythonRuntime_GetPyByteArray_Type,
                   "PyByteArray_Type")
DForwardPythonData(PyTypeObject, LLDBPythonRuntime_GetPyCFunction_Type,
                   "PyCFunction_Type")
DForwardPythonData(PyTypeObject, LLDBPythonRuntime_GetPyFloat_Type,
                   "PyFloat_Type")
DForwardPythonData(PyTypeObject, LLDBPythonRuntime_GetPyModule_Type,
                   "PyModule_Type")
DForwardPythonData(PyTypeObject, LLDBPythonRuntime_GetPyType_Type,
                   "PyType_Type")

DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_AttributeError,
                            "PyExc_AttributeError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_Exception,
                            "PyExc_Exception")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_IndexError,
                            "PyExc_IndexError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_IOError,
                            "PyExc_IOError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_MemoryError,
                            "PyExc_MemoryError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_OverflowError,
                            "PyExc_OverflowError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_RuntimeError,
                            "PyExc_RuntimeError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_SyntaxError,
                            "PyExc_SyntaxError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_SystemError,
                            "PyExc_SystemError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_SystemExit,
                            "PyExc_SystemExit")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_TypeError,
                            "PyExc_TypeError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_ValueError,
                            "PyExc_ValueError")
DForwardPythonObjectPointer(LLDBPythonRuntime_GetPyExc_ZeroDivisionError,
                            "PyExc_ZeroDivisionError")

extern "C" char *(**LLDBPythonRuntime_GetPyOS_ReadlineFunctionPointer())(
    FILE *, FILE *, const char *) {
  using FReadline = char *(*)(FILE *, FILE *, const char *);
  static FReadline *readline_function_pointer =
      fg_ResolvePythonData<FReadline>("PyOS_ReadlineFunctionPointer");
  return readline_function_pointer;
}

extern "C" int PyArg_Parse(PyObject *args, const char *format, ...) {
  va_list varargs;
  va_start(varargs, format);

  int result = 0;
  if (fg_IsTuple(args)) {
    using FFunction = int (*)(PyObject *, const char *, va_list);
    static FFunction fFunction =
        fg_ResolvePythonFunction<FFunction>("PyArg_VaParse");
    result = fFunction(args, format, varargs);
  } else if (format && format[0] && !format[1]) {
    switch (format[0]) {
    case 'b': {
      bool *value = va_arg(varargs, bool *);
      int truth = PyObject_IsTrue(args);
      if (truth >= 0) {
        *value = truth != 0;
        result = 1;
      }
      break;
    }
    case 'c': {
      char *value = va_arg(varargs, char *);
      char *string = nullptr;
      Py_ssize_t size = 0;
      if (PyBytes_AsStringAndSize(args, &string, &size) == 0 && size == 1) {
        *value = string[0];
        result = 1;
      } else if (!PyErr_Occurred()) {
        PyErr_SetString(LLDBPythonRuntime_GetPyExc_TypeError(),
                        "expected a byte string of length 1");
      }
      break;
    }
    case 'd': {
      double *value = va_arg(varargs, double *);
      double parsed = PyFloat_AsDouble(args);
      if (!PyErr_Occurred()) {
        *value = parsed;
        result = 1;
      }
      break;
    }
    case 'f': {
      float *value = va_arg(varargs, float *);
      double parsed = PyFloat_AsDouble(args);
      if (!PyErr_Occurred()) {
        *value = static_cast<float>(parsed);
        result = 1;
      }
      break;
    }
    case 'h': {
      short *value = va_arg(varargs, short *);
      long parsed = PyLong_AsLong(args);
      if (!PyErr_Occurred()) {
        *value = static_cast<short>(parsed);
        result = 1;
      }
      break;
    }
    case 'H': {
      unsigned short *value = va_arg(varargs, unsigned short *);
      unsigned long parsed = PyLong_AsUnsignedLong(args);
      if (!PyErr_Occurred()) {
        *value = static_cast<unsigned short>(parsed);
        result = 1;
      }
      break;
    }
    case 'i': {
      int *value = va_arg(varargs, int *);
      long parsed = PyLong_AsLong(args);
      if (!PyErr_Occurred()) {
        *value = static_cast<int>(parsed);
        result = 1;
      }
      break;
    }
    case 'I': {
      unsigned int *value = va_arg(varargs, unsigned int *);
      unsigned long parsed = PyLong_AsUnsignedLong(args);
      if (!PyErr_Occurred()) {
        *value = static_cast<unsigned int>(parsed);
        result = 1;
      }
      break;
    }
    case 'k': {
      unsigned long *value = va_arg(varargs, unsigned long *);
      unsigned long parsed = PyLong_AsUnsignedLong(args);
      if (!PyErr_Occurred()) {
        *value = parsed;
        result = 1;
      }
      break;
    }
    case 'K': {
      unsigned long long *value = va_arg(varargs, unsigned long long *);
      unsigned long long parsed = PyLong_AsUnsignedLongLong(args);
      if (!PyErr_Occurred()) {
        *value = parsed;
        result = 1;
      }
      break;
    }
    case 'l': {
      long *value = va_arg(varargs, long *);
      long parsed = PyLong_AsLong(args);
      if (!PyErr_Occurred()) {
        *value = parsed;
        result = 1;
      }
      break;
    }
    case 'L': {
      long long *value = va_arg(varargs, long long *);
      long long parsed = PyLong_AsLongLong(args);
      if (!PyErr_Occurred()) {
        *value = parsed;
        result = 1;
      }
      break;
    }
    case 's': {
      char **value = va_arg(varargs, char **);
      Py_ssize_t size = 0;
      const char *parsed = PyUnicode_AsUTF8AndSize(args, &size);
      if (!parsed) {
        PyErr_Clear();
        if (PyBytes_AsStringAndSize(args, const_cast<char **>(&parsed), &size) !=
            0)
          parsed = nullptr;
      }
      if (parsed) {
        *value = const_cast<char *>(parsed);
        result = 1;
      }
      break;
    }
    case 'z': {
      char **value = va_arg(varargs, char **);
      if (args == LLDBPythonRuntime_Get__Py_NoneStruct()) {
        *value = nullptr;
        result = 1;
      } else {
        Py_ssize_t size = 0;
        const char *parsed = PyUnicode_AsUTF8AndSize(args, &size);
        if (parsed) {
          *value = const_cast<char *>(parsed);
          result = 1;
        }
      }
      break;
    }
    default:
      PyErr_Format(LLDBPythonRuntime_GetPyExc_SystemError(),
                   "unsupported PyArg_Parse format: %.200s", format);
      break;
    }
  } else if (format && format[0] == 's' && format[1] == '#') {
    char **value = va_arg(varargs, char **);
    Py_ssize_t size = 0;
    const char *parsed = PyUnicode_AsUTF8AndSize(args, &size);
    if (!parsed) {
      PyErr_Clear();
      if (PyBytes_AsStringAndSize(args, const_cast<char **>(&parsed), &size) !=
          0)
        parsed = nullptr;
    }
    if (parsed) {
      *value = const_cast<char *>(parsed);
      result = 1;
    }
  } else {
    PyErr_Format(LLDBPythonRuntime_GetPyExc_SystemError(),
                 "unsupported PyArg_Parse format: %.200s",
                 format ? format : "");
  }

  va_end(varargs);
  return result;
}

extern "C" int PyArg_UnpackTuple(PyObject *args, const char *name,
                                  Py_ssize_t min, Py_ssize_t max, ...) {
  if (!fg_IsTuple(args)) {
    PyErr_SetString(LLDBPythonRuntime_GetPyExc_SystemError(),
                    "PyArg_UnpackTuple() argument list is not a tuple");
    return 0;
  }

  Py_ssize_t size = PyTuple_Size(args);
  if (size < min || size > max) {
    PyErr_Format(LLDBPythonRuntime_GetPyExc_TypeError(),
                 "%s expected %zd to %zd arguments, got %zd",
                 name ? name : "function", min, max, size);
    return 0;
  }

  va_list varargs;
  va_start(varargs, max);
  for (Py_ssize_t i = 0; i < size; ++i) {
    PyObject **object = va_arg(varargs, PyObject **);
    *object = PyTuple_GetItem(args, i);
  }
  va_end(varargs);
  return 1;
}

DForwardPythonFunction(PyObject *, PyBool_FromLong, (long value), (value))
DForwardPythonFunction(char *, PyByteArray_AsString, (PyObject *object),
                       (object))
DForwardPythonFunction(Py_ssize_t, PyByteArray_Size, (PyObject *object),
                       (object))
DForwardPythonFunction(char *, PyBytes_AsString, (PyObject *object), (object))
DForwardPythonFunction(int, PyBytes_AsStringAndSize,
                       (PyObject *object, char **buffer, Py_ssize_t *length),
                       (object, buffer, length))
DForwardPythonFunction(PyObject *, PyBytes_FromStringAndSize,
                       (const char *string, Py_ssize_t length),
                       (string, length))
DForwardPythonFunction(Py_ssize_t, PyBytes_Size, (PyObject *object), (object))
DForwardPythonFunction(int, PyCallable_Check, (PyObject *object), (object))
DForwardPythonFunction(void *, PyCapsule_GetPointer,
                       (PyObject *capsule, const char *name), (capsule, name))
DForwardPythonFunction(void *, PyCapsule_Import,
                       (const char *name, int no_block), (name, no_block))
DForwardPythonFunction(PyObject *, PyCapsule_New,
                       (void *pointer, const char *name,
                        PyCapsule_Destructor destructor),
                       (pointer, name, destructor))
DForwardPythonFunction(int, PyCFunction_GetFlags, (PyObject *object), (object))
DForwardPythonFunction(PyCFunction, PyCFunction_GetFunction,
                       (PyObject *object), (object))
DForwardPythonFunction(PyObject *, PyCFunction_GetSelf, (PyObject *object),
                       (object))
DForwardPythonFunction(PyObject *, Py_CompileString,
                       (const char *string, const char *filename, int start),
                       (string, filename, start))
DForwardPythonVoidFunction(_Py_Dealloc, (PyObject *object), (object))
DForwardPythonVoidFunction(Py_DecRef, (PyObject *object), (object))
DForwardPythonVoidFunction(Py_IncRef, (PyObject *object), (object))
DForwardPythonFunction(int, PyDict_Contains,
                       (PyObject *dict, PyObject *key), (dict, key))
DForwardPythonFunction(PyObject *, PyDict_GetItem,
                       (PyObject *dict, PyObject *key), (dict, key))
DForwardPythonFunction(PyObject *, PyDict_GetItemString,
                       (PyObject *dict, const char *key), (dict, key))
DForwardPythonFunction(PyObject *, PyDict_GetItemWithError,
                       (PyObject *dict, PyObject *key), (dict, key))
DForwardPythonFunction(PyObject *, PyDict_Keys, (PyObject *dict), (dict))
DForwardPythonFunction(PyObject *, PyDict_New, (), ())
DForwardPythonFunction(int, PyDict_SetItem,
                       (PyObject *dict, PyObject *key, PyObject *value),
                       (dict, key, value))
DForwardPythonFunction(int, PyDict_SetItemString,
                       (PyObject *dict, const char *key, PyObject *value),
                       (dict, key, value))
DForwardPythonVoidFunction(PyErr_Clear, (), ())
DForwardPythonFunction(int, PyErr_ExceptionMatches, (PyObject *exception),
                       (exception))
DForwardPythonVoidFunction(PyErr_Fetch,
                           (PyObject **type, PyObject **value,
                            PyObject **traceback),
                           (type, value, traceback))
extern "C" PyObject *PyErr_Format(PyObject *exception, const char *format, ...) {
  using FFunction = PyObject *(*)(PyObject *, const char *, va_list);
  static FFunction fFunction =
      fg_ResolvePythonFunction<FFunction>("PyErr_FormatV");

  va_list varargs;
  va_start(varargs, format);
  PyObject *result = fFunction(exception, format, varargs);
  va_end(varargs);
  return result;
}
DForwardPythonFunction(int, PyErr_GivenExceptionMatches,
                       (PyObject *given, PyObject *exception),
                       (given, exception))
DForwardPythonVoidFunction(PyErr_NormalizeException,
                           (PyObject **type, PyObject **value,
                            PyObject **traceback),
                           (type, value, traceback))
DForwardPythonFunction(PyObject *, PyErr_Occurred, (), ())
DForwardPythonVoidFunction(PyErr_Print, (), ())
DForwardPythonVoidFunction(PyErr_Restore,
                           (PyObject *type, PyObject *value,
                            PyObject *traceback),
                           (type, value, traceback))
DForwardPythonVoidFunction(PyErr_SetInterrupt, (), ())
DForwardPythonVoidFunction(PyErr_SetString,
                           (PyObject *exception, const char *string),
                           (exception, string))
DForwardPythonVoidFunction(PyErr_WriteUnraisable, (PyObject *object), (object))
DForwardPythonFunction(PyObject *, PyEval_EvalCode,
                       (PyObject *code, PyObject *globals, PyObject *locals),
                       (code, globals, locals))
DForwardPythonVoidFunction(PyEval_RestoreThread, (PyThreadState *state),
                           (state))
DForwardPythonFunction(PyThreadState *, PyEval_SaveThread, (), ())
DForwardPythonFunction(PyObject *, PyFile_FromFd,
                       (int fd, const char *name, const char *mode,
                        int buffering, const char *encoding,
                        const char *errors, const char *newline,
                        int closefd),
                       (fd, name, mode, buffering, encoding, errors, newline,
                        closefd))
DForwardPythonFunction(double, PyFloat_AsDouble, (PyObject *object), (object))
DForwardPythonFunction(PyObject *, PyFloat_FromDouble, (double value), (value))
DForwardPythonFunction(PyGILState_STATE, PyGILState_Ensure, (), ())
DForwardPythonVoidFunction(PyGILState_Release, (PyGILState_STATE state),
                           (state))
DForwardPythonFunction(PyObject *, PyImport_AddModule, (const char *name),
                       (name))
DForwardPythonFunction(int, PyImport_AppendInittab,
                       (const char *name, PyObject *(*initfunc)()),
                       (name, initfunc))
DForwardPythonFunction(PyObject *, PyImport_ImportModule, (const char *name),
                       (name))
DForwardPythonVoidFunction(Py_InitializeEx, (int install_sigs),
                           (install_sigs))
DForwardPythonFunction(int, Py_IsInitialized, (), ())
DForwardPythonFunction(int, PyList_Append,
                       (PyObject *list, PyObject *item), (list, item))
DForwardPythonFunction(PyObject *, PyList_AsTuple, (PyObject *list), (list))
DForwardPythonFunction(PyObject *, PyList_GetItem,
                       (PyObject *list, Py_ssize_t index), (list, index))
DForwardPythonFunction(PyObject *, PyList_New, (Py_ssize_t size), (size))
DForwardPythonFunction(int, PyList_SetItem,
                       (PyObject *list, Py_ssize_t index, PyObject *item),
                       (list, index, item))
DForwardPythonFunction(Py_ssize_t, PyList_Size, (PyObject *list), (list))
DForwardPythonFunction(double, PyLong_AsDouble, (PyObject *object), (object))
DForwardPythonFunction(long, PyLong_AsLong, (PyObject *object), (object))
DForwardPythonFunction(long long, PyLong_AsLongLong, (PyObject *object),
                       (object))
DForwardPythonFunction(unsigned long, PyLong_AsUnsignedLong,
                       (PyObject *object), (object))
DForwardPythonFunction(unsigned long long, PyLong_AsUnsignedLongLong,
                       (PyObject *object), (object))
DForwardPythonFunction(PyObject *, PyLong_FromLong, (long value), (value))
DForwardPythonFunction(PyObject *, PyLong_FromLongLong, (long long value),
                       (value))
DForwardPythonFunction(PyObject *, PyLong_FromSize_t, (size_t value), (value))
DForwardPythonFunction(PyObject *, PyLong_FromUnsignedLong,
                       (unsigned long value), (value))
DForwardPythonFunction(PyObject *, PyLong_FromUnsignedLongLong,
                       (unsigned long long value), (value))
DForwardPythonFunction(PyObject *, PyLong_FromVoidPtr, (void *pointer),
                       (pointer))
DForwardPythonFunction(PyObject *, PyMemoryView_FromMemory,
                       (char *memory, Py_ssize_t size, int flags),
                       (memory, size, flags))
DForwardPythonVoidFunction(PyMem_RawFree, (void *pointer), (pointer))
DForwardPythonFunction(void *, PyMem_RawMalloc, (size_t size), (size))
DForwardPythonFunction(int, PyModule_AddObject,
                       (PyObject *module, const char *name, PyObject *value),
                       (module, name, value))
DForwardPythonFunction(PyObject *, PyModuleDef_Init, (PyModuleDef *module),
                       (module))
DForwardPythonFunction(PyObject *, PyModule_Create2,
                       (PyModuleDef *module, int apiver), (module, apiver))
DForwardPythonFunction(PyObject *, PyModule_GetDict, (PyObject *module),
                       (module))
DForwardPythonFunction(int, PyObject_AsFileDescriptor, (PyObject *object),
                       (object))
DForwardPythonFunction(PyObject *, PyObject_Call,
                       (PyObject *callable, PyObject *args, PyObject *kwargs),
                       (callable, args, kwargs))
extern "C" PyObject *PyObject_CallFunction(PyObject *callable,
                                           const char *format, ...) {
  va_list varargs;
  va_start(varargs, format);
  PyObject *args = fg_PyVaBuildValue(format ? format : "()", varargs);
  va_end(varargs);
  if (!args)
    return nullptr;

  if (!fg_IsTuple(args)) {
    PyObject *tuple = PyTuple_New(1);
    if (!tuple) {
      Py_DECREF(args);
      return nullptr;
    }
    PyTuple_SetItem(tuple, 0, args);
    args = tuple;
  }

  PyObject *result = PyObject_CallObject(callable, args);
  Py_DECREF(args);
  return result;
}

extern "C" PyObject *PyObject_CallFunctionObjArgs(PyObject *callable, ...) {
  std::vector<PyObject *> objects;
  va_list varargs;
  va_start(varargs, callable);
  while (PyObject *object = va_arg(varargs, PyObject *))
    objects.push_back(object);
  va_end(varargs);

  PyObject *args = PyTuple_New(objects.size());
  if (!args)
    return nullptr;

  for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(objects.size()); ++i) {
    Py_INCREF(objects[i]);
    PyTuple_SetItem(args, i, objects[i]);
  }

  PyObject *result = PyObject_CallObject(callable, args);
  Py_DECREF(args);
  return result;
}

extern "C" PyObject *PyObject_CallMethod(PyObject *object, const char *method,
                                         const char *format, ...) {
  PyObject *callable = PyObject_GetAttrString(object, method);
  if (!callable)
    return nullptr;

  va_list varargs;
  va_start(varargs, format);
  PyObject *args = fg_PyVaBuildValue(format ? format : "()", varargs);
  va_end(varargs);
  if (!args) {
    Py_DECREF(callable);
    return nullptr;
  }

  if (!fg_IsTuple(args)) {
    PyObject *tuple = PyTuple_New(1);
    if (!tuple) {
      Py_DECREF(args);
      Py_DECREF(callable);
      return nullptr;
    }
    PyTuple_SetItem(tuple, 0, args);
    args = tuple;
  }

  PyObject *result = PyObject_CallObject(callable, args);
  Py_DECREF(args);
  Py_DECREF(callable);
  return result;
}

extern "C" PyObject *PyObject_CallMethodObjArgs(PyObject *object,
                                                PyObject *method, ...) {
  PyObject *callable = PyObject_GetAttr(object, method);
  if (!callable)
    return nullptr;

  std::vector<PyObject *> objects;
  va_list varargs;
  va_start(varargs, method);
  while (PyObject *argument = va_arg(varargs, PyObject *))
    objects.push_back(argument);
  va_end(varargs);

  PyObject *args = PyTuple_New(objects.size());
  if (!args) {
    Py_DECREF(callable);
    return nullptr;
  }

  for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(objects.size()); ++i) {
    Py_INCREF(objects[i]);
    PyTuple_SetItem(args, i, objects[i]);
  }

  PyObject *result = PyObject_CallObject(callable, args);
  Py_DECREF(args);
  Py_DECREF(callable);
  return result;
}

DForwardPythonFunction(PyObject *, PyObject_CallObject,
                       (PyObject *callable, PyObject *args), (callable, args))
DForwardPythonVoidFunction(PyObject_Free, (void *pointer), (pointer))
DForwardPythonFunction(PyObject *, PyObject_GenericGetAttr,
                       (PyObject *object, PyObject *name), (object, name))
DForwardPythonFunction(PyObject *, PyObject_GetAttr,
                       (PyObject *object, PyObject *name), (object, name))
DForwardPythonFunction(PyObject *, PyObject_GetAttrString,
                       (PyObject *object, const char *name), (object, name))
DForwardPythonFunction(int, PyObject_HasAttr,
                       (PyObject *object, PyObject *name), (object, name))
DForwardPythonFunction(int, PyObject_IsInstance,
                       (PyObject *object, PyObject *type), (object, type))
DForwardPythonFunction(int, PyObject_IsTrue, (PyObject *object), (object))
DForwardPythonFunction(PyObject *, _PyObject_New, (PyTypeObject *type), (type))
DForwardPythonFunction(PyObject *, PyObject_Repr, (PyObject *object), (object))
DForwardPythonFunction(int, PyObject_SetAttr,
                       (PyObject *object, PyObject *name, PyObject *value),
                       (object, name, value))
DForwardPythonFunction(PyObject *, PyObject_Str, (PyObject *object), (object))
DForwardPythonFunction(PyObject *, PyObject_Type, (PyObject *object), (object))
DForwardPythonFunction(PyThreadState *, PyThreadState_Get, (), ())
DForwardPythonFunction(PyObject *, PyThreadState_GetDict, (), ())
DForwardPythonFunction(PyObject *, PyTuple_GetItem,
                       (PyObject *tuple, Py_ssize_t index), (tuple, index))
DForwardPythonFunction(PyObject *, PyTuple_New, (Py_ssize_t size), (size))
DForwardPythonFunction(int, PyTuple_SetItem,
                       (PyObject *tuple, Py_ssize_t index, PyObject *item),
                       (tuple, index, item))
DForwardPythonFunction(Py_ssize_t, PyTuple_Size, (PyObject *tuple), (tuple))
DForwardPythonFunction(PyObject *, PyType_FromSpec, (PyType_Spec *spec), (spec))
DForwardPythonFunction(unsigned long, PyType_GetFlags, (PyTypeObject *type),
                       (type))
DForwardPythonFunction(void *, PyType_GetSlot, (PyTypeObject *type, int slot),
                       (type, slot))
DForwardPythonFunction(int, PyType_IsSubtype,
                       (PyTypeObject *type, PyTypeObject *base),
                       (type, base))
DForwardPythonVoidFunction(PyType_Modified, (PyTypeObject *type), (type))
DForwardPythonFunction(PyObject *, PyUnicode_AsEncodedString,
                       (PyObject *unicode, const char *encoding,
                        const char *errors),
                       (unicode, encoding, errors))
DForwardPythonFunction(PyObject *, PyUnicode_AsUTF8String, (PyObject *unicode),
                       (unicode))
DForwardPythonFunction(const char *, PyUnicode_AsUTF8AndSize,
                       (PyObject *unicode, Py_ssize_t *size), (unicode, size))
DForwardPythonFunction(int, PyUnicode_CompareWithASCIIString,
                       (PyObject *unicode, const char *string),
                       (unicode, string))
DForwardPythonFunction(PyObject *, PyUnicode_Concat,
                       (PyObject *left, PyObject *right), (left, right))
DForwardPythonFunction(PyObject *, PyUnicode_DecodeUTF8,
                       (const char *string, Py_ssize_t size,
                        const char *errors),
                       (string, size, errors))
extern "C" PyObject *PyUnicode_FromFormat(const char *format, ...) {
  using FFunction = PyObject *(*)(const char *, va_list);
  static FFunction fFunction =
      fg_ResolvePythonFunction<FFunction>("PyUnicode_FromFormatV");

  va_list varargs;
  va_start(varargs, format);
  PyObject *result = fFunction(format, varargs);
  va_end(varargs);
  return result;
}
DForwardPythonFunction(PyObject *, PyUnicode_FromString,
                       (const char *string), (string))
DForwardPythonFunction(PyObject *, PyUnicode_FromStringAndSize,
                       (const char *string, Py_ssize_t size), (string, size))
DForwardPythonFunction(Py_ssize_t, PyUnicode_GetLength, (PyObject *unicode),
                       (unicode))
DForwardPythonFunction(PyObject *, PyUnicode_InternFromString,
                       (const char *string), (string))

extern "C" PyObject *Py_BuildValue(const char *format, ...) {
  using FFunction = PyObject *(*)(const char *, va_list);
  static FFunction fFunction =
      fg_ResolvePythonFunction<FFunction>("Py_VaBuildValue");

  va_list varargs;
  va_start(varargs, format);
  PyObject *result = fFunction(format, varargs);
  va_end(varargs);
  return result;
}

#undef DForwardPythonFunction
#undef DForwardPythonVoidFunction

#endif
