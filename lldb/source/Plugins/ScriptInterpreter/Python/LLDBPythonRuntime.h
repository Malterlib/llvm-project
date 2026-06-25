//===-- LLDBPythonRuntime.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_LLDBPYTHONRUNTIME_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_LLDBPYTHONRUNTIME_H

#include <cstdio>
#include <cstdlib>

struct _object;
typedef _object PyObject;
typedef struct _typeobject PyTypeObject;

extern "C" bool LLDBPythonRuntime_Initialize();

extern "C" PyObject *LLDBPythonRuntime_Get__Py_FalseStruct();
extern "C" PyObject *LLDBPythonRuntime_Get__Py_NoneStruct();
extern "C" PyObject *LLDBPythonRuntime_Get__Py_NotImplementedStruct();
extern "C" PyObject *LLDBPythonRuntime_Get__Py_TrueStruct();

extern "C" PyTypeObject *LLDBPythonRuntime_GetPyBool_Type();
extern "C" PyTypeObject *LLDBPythonRuntime_GetPyByteArray_Type();
extern "C" PyTypeObject *LLDBPythonRuntime_GetPyCFunction_Type();
extern "C" PyTypeObject *LLDBPythonRuntime_GetPyFloat_Type();
extern "C" PyTypeObject *LLDBPythonRuntime_GetPyModule_Type();
extern "C" PyTypeObject *LLDBPythonRuntime_GetPyType_Type();

extern "C" PyObject *LLDBPythonRuntime_GetPyExc_AttributeError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_Exception();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_IndexError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_IOError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_MemoryError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_OverflowError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_RuntimeError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_SyntaxError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_SystemError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_SystemExit();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_TypeError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_ValueError();
extern "C" PyObject *LLDBPythonRuntime_GetPyExc_ZeroDivisionError();

extern "C" char *(**LLDBPythonRuntime_GetPyOS_ReadlineFunctionPointer())(
    FILE *, FILE *, const char *);

extern "C" void *PyMem_RawMalloc(size_t size);
extern "C" void PyMem_RawFree(void *pointer);

#if LLDB_DYNAMIC_PYTHON_RUNTIME && !defined(LLDB_PYTHON_RUNTIME_IMPLEMENTATION)

#if PY_VERSION_HEX < 0x030A0000
static inline PyObject *Py_NewRef(PyObject *object) {
  Py_INCREF(object);
  return object;
}
#endif

#undef Py_False
#define Py_False LLDBPythonRuntime_Get__Py_FalseStruct()

#undef Py_None
#define Py_None LLDBPythonRuntime_Get__Py_NoneStruct()

#undef Py_NotImplemented
#define Py_NotImplemented LLDBPythonRuntime_Get__Py_NotImplementedStruct()

#undef Py_True
#define Py_True LLDBPythonRuntime_Get__Py_TrueStruct()

#undef Py_RETURN_FALSE
#define Py_RETURN_FALSE return Py_NewRef(Py_False)

#undef Py_RETURN_NONE
#define Py_RETURN_NONE return Py_NewRef(Py_None)

#undef Py_RETURN_NOTIMPLEMENTED
#define Py_RETURN_NOTIMPLEMENTED return Py_NewRef(Py_NotImplemented)

#undef Py_RETURN_TRUE
#define Py_RETURN_TRUE return Py_NewRef(Py_True)

#undef PyBool_Type
#define PyBool_Type (*LLDBPythonRuntime_GetPyBool_Type())

#undef PyByteArray_Type
#define PyByteArray_Type (*LLDBPythonRuntime_GetPyByteArray_Type())

#undef PyCFunction_Type
#define PyCFunction_Type (*LLDBPythonRuntime_GetPyCFunction_Type())

#undef PyFloat_Type
#define PyFloat_Type (*LLDBPythonRuntime_GetPyFloat_Type())

#undef PyModule_Type
#define PyModule_Type (*LLDBPythonRuntime_GetPyModule_Type())

#undef PyType_Type
#define PyType_Type (*LLDBPythonRuntime_GetPyType_Type())

#undef PyExc_AttributeError
#define PyExc_AttributeError LLDBPythonRuntime_GetPyExc_AttributeError()

#undef PyExc_Exception
#define PyExc_Exception LLDBPythonRuntime_GetPyExc_Exception()

#undef PyExc_IndexError
#define PyExc_IndexError LLDBPythonRuntime_GetPyExc_IndexError()

#undef PyExc_IOError
#define PyExc_IOError LLDBPythonRuntime_GetPyExc_IOError()

#undef PyExc_MemoryError
#define PyExc_MemoryError LLDBPythonRuntime_GetPyExc_MemoryError()

#undef PyExc_OverflowError
#define PyExc_OverflowError LLDBPythonRuntime_GetPyExc_OverflowError()

#undef PyExc_RuntimeError
#define PyExc_RuntimeError LLDBPythonRuntime_GetPyExc_RuntimeError()

#undef PyExc_SyntaxError
#define PyExc_SyntaxError LLDBPythonRuntime_GetPyExc_SyntaxError()

#undef PyExc_SystemError
#define PyExc_SystemError LLDBPythonRuntime_GetPyExc_SystemError()

#undef PyExc_SystemExit
#define PyExc_SystemExit LLDBPythonRuntime_GetPyExc_SystemExit()

#undef PyExc_TypeError
#define PyExc_TypeError LLDBPythonRuntime_GetPyExc_TypeError()

#undef PyExc_ValueError
#define PyExc_ValueError LLDBPythonRuntime_GetPyExc_ValueError()

#undef PyExc_ZeroDivisionError
#define PyExc_ZeroDivisionError LLDBPythonRuntime_GetPyExc_ZeroDivisionError()

#undef PyOS_ReadlineFunctionPointer
#define PyOS_ReadlineFunctionPointer                                          \
  (*LLDBPythonRuntime_GetPyOS_ReadlineFunctionPointer())

#endif

#endif
