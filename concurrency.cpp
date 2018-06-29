#include "concurrency.h"
#include "error.h"


namespace ipc {

///////////////////

ManualResetEvent::ManualResetEvent()
   : handle_(CreateEvent(nullptr, TRUE, TRUE, nullptr))
{
   if (!handle_)
      throw Error("Win API CreateEvent failed.", GetLastError());
}


ManualResetEvent::~ManualResetEvent()
{
   CloseHandle(handle_);
}


void ManualResetEvent::Signal()
{
   if (!SetEvent(handle_))
      throw Error("Win API SetEvent failed.", GetLastError());
}


void ManualResetEvent::Reset()
{
   if (!ResetEvent(handle_))
      throw Error("Win API ResetEvent failed.", GetLastError());
}

} // namespace ipc
