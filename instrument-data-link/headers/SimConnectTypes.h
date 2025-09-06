#pragma once

#ifndef _SIMCONNECT_TYPES_H_
#define _SIMCONNECT_TYPES_H_

#ifdef _WIN32
#include <Windows.h>
#else
// Basic Windows types for Linux
typedef void* HANDLE;
typedef void* HWND;
typedef unsigned long DWORD;
typedef unsigned long long UINT64;
typedef int BOOL;
typedef const char* LPCSTR;
typedef unsigned char BYTE;
typedef long HRESULT;
struct GUID { char dummy[16]; };

// Windows macros
#define FALSE 0
#define TRUE 1
#define CALLBACK
#define __stdcall
#define MAX_PATH 260
#define __int64 long long

// SimConnect specific types
typedef DWORD SIMCONNECT_OBJECT_ID;
typedef DWORD SIMCONNECT_DATA_DEFINITION_ID;
typedef DWORD SIMCONNECT_DATA_REQUEST_ID;
typedef DWORD SIMCONNECT_CLIENT_EVENT_ID;
typedef DWORD SIMCONNECT_CLIENT_DATA_ID;
typedef DWORD SIMCONNECT_CLIENT_DATA_DEFINITION_ID;
typedef DWORD SIMCONNECT_NOTIFICATION_GROUP_ID;
typedef DWORD SIMCONNECT_INPUT_GROUP_ID;
typedef DWORD SIMCONNECT_EVENT_FLAG;
typedef DWORD SIMCONNECT_DATA_SET_FLAG;
typedef DWORD SIMCONNECT_CLIENT_DATA_SET_FLAG;
typedef DWORD SIMCONNECT_VIEW_SYSTEM_EVENT_DATA;

// SimConnect macros
#define SIMCONNECT_REFSTRUCT struct
#define SIMCONNECT_STRUCT struct
#define SIMCONNECT_STRING(name, size) char name[size]
#define SIMCONNECT_GUID GUID
#define SIMCONNECT_STRINGV(name) char name[1]
#define SIMCONNECT_DATAV(name, id, count) DWORD name
#define SIMCONNECT_FIXEDTYPE_DATAV(type, name, count, cliMarshalAs, cliType) type name[1]
#define SIMCONNECT_ENUM enum
#define SIMCONNECT_ENUM_FLAGS typedef DWORD
#define SIMCONNECT_USER_ENUM typedef DWORD

// Define SIMCONNECTAPI for Linux
#define SIMCONNECTAPI extern "C" HRESULT __stdcall

#endif // _WIN32
#endif // _SIMCONNECT_TYPES_H_
