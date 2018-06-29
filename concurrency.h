#pragma once
#include "ipc_api_decl.h"
#include "win.h"


namespace ipc {

///////////////////

class IPC_API ManualResetEvent
{
public:
   ManualResetEvent();
   ~ManualResetEvent();
   ManualResetEvent(const ManualResetEvent&) = delete;
   ManualResetEvent(ManualResetEvent&&) = default;
   ManualResetEvent& operator=(const ManualResetEvent&) = delete;
   ManualResetEvent& operator=(ManualResetEvent&&) = default;

   HANDLE Handle() const { return handle_; }
   void Signal();
   void Reset();

private:
   HANDLE handle_;
};

} // namespace ipc
