#pragma once

#ifdef IPC_API_DLL
#   define IPC_API __declspec(dllexport)
#else
#   define IPC_API __declspec(dllimport)
#endif
