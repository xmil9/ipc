#pragma once
#include "win.h"


namespace ipc {

struct Handle
{
   Handle() : h(NULL) {}
   Handle(HANDLE h) : h(h) {}
   ~Handle();
   Handle(const Handle&) = delete;
   Handle(Handle&&) = default;
   Handle& operator=(const Handle&) = delete;
   Handle& operator=(Handle&&) = default;

   explicit operator bool() const;
   HANDLE h;
};


inline Handle::~Handle()
{
   if (h && h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
}


inline Handle::operator bool() const
{
   return (h != NULL && h != INVALID_HANDLE_VALUE);
}

} // namespace ipc