/* execinfo.h
Implements backtrace() et al from glibc on win64
(C) 2016 Niall Douglas http://www.nedprod.com/
File Created: Mar 2016


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "../include/execinfo_win64.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "windows.h"

#ifdef __cplusplus
namespace
{
#endif
  typedef struct _IMAGEHLP_LINE64
  {
    DWORD SizeOfStruct;
    PVOID Key;
    DWORD LineNumber;
    PTSTR FileName;
    DWORD64 Address;
  } IMAGEHLP_LINE64, *PIMAGEHLP_LINE64;

  typedef BOOL(WINAPI *SymInitialize_t)(_In_ HANDLE hProcess, _In_opt_ PCTSTR UserSearchPath, _In_ BOOL fInvadeProcess);

  typedef BOOL(WINAPI *SymGetLineFromAddr64_t)(_In_ HANDLE hProcess, _In_ DWORD64 dwAddr, _Out_ PDWORD pdwDisplacement, _Out_ PIMAGEHLP_LINE64 Line);

  static HMODULE dbghelp;
  static SymInitialize_t SymInitialize;
  static SymGetLineFromAddr64_t SymGetLineFromAddr64;

  static void load_dbghelp()
  {
    if(dbghelp)
      return;
    dbghelp = LoadLibraryA("DBGHELP.DLL");
    if(dbghelp)
    {
      SymInitialize = (SymInitialize_t) GetProcAddress(dbghelp, "SymInitializeW");
      if(!SymInitialize)
        abort();
      if(!SymInitialize(GetCurrentProcess(), NULL, TRUE))
        abort();
      SymGetLineFromAddr64 = (SymGetLineFromAddr64_t) GetProcAddress(dbghelp, "SymGetLineFromAddrW64");
      if(!SymGetLineFromAddr64)
        abort();
    }
  }

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

_Check_return_ size_t backtrace(_Out_writes_(len) void **bt, _In_ size_t len)
{
  return RtlCaptureStackBackTrace(1, (DWORD) len, bt, NULL);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6385 6386)  // MSVC static analyser can't grok this function. clang's analyser gives it thumbs up.
#endif
_Check_return_ _Ret_writes_maybenull_(len) char **backtrace_symbols(_In_reads_(len) void *const *bt, _In_ size_t len)
{
  size_t bytes = (len + 1) * sizeof(void *) + 256, n;
  if(!len)
    return NULL;
  else
  {
    char **ret = (char **) malloc(bytes);
    char *p = (char *) (ret + len + 1), *end = (char *) ret + bytes;
    if(!ret)
      return NULL;
    for(n = 0; n < len + 1; n++)
      ret[n] = NULL;
    load_dbghelp();
    for(n = 0; n < len; n++)
    {
      DWORD displ;
      IMAGEHLP_LINE64 ihl;
      memset(&ihl, 0, sizeof(ihl));
      ihl.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
      int please_realloc = 0;
      if(!bt[n])
      {
        ret[n] = NULL;
      }
      else
      {
        // Keep offset till later
        ret[n] = (char *) ((char *) p - (char *) ret);
        if(!SymGetLineFromAddr64 || !SymGetLineFromAddr64(GetCurrentProcess(), (size_t) bt[n], &displ, &ihl))
        {
          if(n == 0)
          {
            free(ret);
            return NULL;
          }
          ihl.FileName = (wchar_t *) L"unknown";
          ihl.LineNumber = 0;
        }
      retry:
        if(please_realloc)
        {
          char **temp = (char **) realloc(ret, bytes + 256);
          if(!temp)
          {
            free(ret);
            return NULL;
          }
          p = (char *) temp + (p - (char *) ret);
          ret = temp;
          bytes += 256;
          end = (char *) ret + bytes;
        }
        if(ihl.FileName && ihl.FileName[0])
        {
          int plen = WideCharToMultiByte(CP_UTF8, 0, ihl.FileName, -1, p, (int) (end - p), NULL, NULL);
          if(!plen)
          {
            please_realloc = 1;
            goto retry;
          }
          p[plen - 1] = 0;
          p += plen - 1;
        }
        else
        {
          if(end - p < 16)
          {
            please_realloc = 1;
            goto retry;
          }
          _ui64toa_s((size_t) bt[n], p, end - p, 16);
          p = strchr(p, 0);
        }
        if(end - p < 16)
        {
          please_realloc = 1;
          goto retry;
        }
        *p++ = ':';
        _itoa_s(ihl.LineNumber, p, end - p, 10);
        p = strchr(p, 0) + 1;
      }
    }
    for(n = 0; n < len; n++)
    {
      if(ret[n])
        ret[n] = (char *) ret + (size_t) ret[n];
    }
    return ret;
  }
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// extern void backtrace_symbols_fd(void *const *bt, size_t len, int fd);

#ifdef __cplusplus
}
#endif
