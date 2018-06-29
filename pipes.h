#pragma once
#include "concurrency.h"
#include "error.h"
#include "win.h"
#include "win_util.h"
#include <cassert>
#include <cstring>
#include <algorithm>
#include <array>
#include <string>


namespace ipc {

///////////////////

// Default data buffer capacity of pipes.
constexpr size_t DefaultBufferCapacity = 4096;


///////////////////

template <size_t N, size_t M> class CrPipe;

// Callbacks for data received and sent through the pipe. Derive and implement to define
// the custom behavior of the server-side pipe.
// Kept outside of pipe class to make life for client code easier by not having to deal
// with the complicated pipe class and the fact that pipe instance need to be allocated
// on the heap. This leaves the heap allocation requirement an implentation detail of the
// pipe server.
template <size_t N, size_t M = N>
struct CrPipeCallbacks
{
   virtual void OnPipeConnected(CrPipe<N, M>& pipe);
   virtual void OnDataReceived(CrPipe<N, M>& pipe, const uint8_t* data,
                               size_t dataSize);
   // The pipe received data from a client but not all data fit into the pipe buffer.
   virtual void OnPartialDataReceived(CrPipe<N, M>& pipe, const uint8_t* data,
                                      size_t dataSize);
   virtual void OnDataSent(CrPipe<N, M>& pipe);

protected:
   // No deleting through this type.
   ~CrPipeCallbacks() {}
};


///////////////////

// Server-side pipe connecting a 'completion routine' pipe server to clients.
// Heap allocation requirement: Instances delete themselves, therefore they need to be
// allocated on the heap!
template <size_t N, size_t M = N>
class CrPipe
{
public:
   explicit CrPipe(CrPipeCallbacks<N, M>& callbacks);

   HANDLE PipeHandle() const { return pipeHandle_.h; }

   void Connect(const std::string& name, HANDLE connectedEvent);
   void OnConnected();
   // Diconnects the pipe and deletes itself. Do not use this pipe instance after a
   // call to Disconnect.
   void Disconnect();
   
   void ListenForData();
   void SendData(const uint8_t* data, size_t dataSize);
   
private:
   // Information about pipe that gets passed to the system. Attached is a pointer to
   // the pipe instance itself to allow accessing it from the completion routines.
   // Has to start with an OVERLAPPED structure because that's what we pass to the
   // Win API pipe calls.
   struct PipeInfo
   {
      OVERLAPPED state;
      CrPipe<N, M>* self = nullptr;

      PipeInfo() { Clear(); }
      void Clear();
      void Set(HANDLE connectedEvent, CrPipe<N, M>* pipe);
   };

private:
   // To ensure that instances can only be deleted by class methods.
   ~CrPipe() = default;

   // I/O completion routines.
   static void WINAPI ReadCompleted(DWORD err, DWORD numBytesRead, OVERLAPPED* pipeInfo);
   static void WINAPI WriteCompleted(DWORD err, DWORD numBytesRead, OVERLAPPED* pipeInfo);

   // Non-static methods called by the completion routines. 
   void OnReadCompleted(DWORD err, DWORD numBytesRead);
   void OnWriteCompleted(DWORD err, DWORD numBytesRead);

 private:
   PipeInfo info_;
   Handle pipeHandle_;
   std::array<uint8_t, N> readBuffer_;
   std::array<uint8_t, M> writeBuffer_;
   CrPipeCallbacks<N, M>& callbacks_;
   bool isConnectionPending_ = false;
   bool isConnected_ = false;
};


template <size_t N, size_t M>
void CrPipe<N, M>::PipeInfo::Clear()
{
   memset(&state, 0, sizeof(state));
   self = nullptr;
}


template <size_t N, size_t M>
void CrPipe<N, M>::PipeInfo::Set(HANDLE connectedEvent, CrPipe<N, M>* pipe)
{
   Clear();
   state.hEvent = connectedEvent;
   self = pipe;
}


template <size_t N, size_t M>
CrPipe<N, M>::CrPipe(CrPipeCallbacks<N, M>& callbacks)
: callbacks_(callbacks)
{
}


template <size_t N, size_t M>
void CrPipe<N, M>::Connect(const std::string& name, HANDLE connectedEvent)
{
   info_.Set(connectedEvent, this);

   constexpr DWORD clientTimeOut = 5000;
   pipeHandle_.h = CreateNamedPipeA(
      name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
      N, N, clientTimeOut, nullptr);
   if (pipeHandle_.h == INVALID_HANDLE_VALUE)
      throw Error("Win API CreateNamedPipe failed.", GetLastError());

   // For overlapped pipe operations ConnectNamedPipe returns immediately.
   ConnectNamedPipe(pipeHandle_.h, reinterpret_cast<OVERLAPPED*>(&info_));
   switch (GetLastError())
   {
   case ERROR_IO_PENDING:
      // Connection is pending. This is the "normal' case.
      isConnectionPending_ = true;
      break;
   case ERROR_PIPE_CONNECTED:
      // A client has already connected in the time between the CreateNamedPipe and
      // ConnectNamedPipe calls. Signal the 'connected' event.
      if (!SetEvent(connectedEvent))
         throw Error("Win API SetEvent failed.", GetLastError());
      break;
   default:
      throw Error("Win API ConnectNamedPipe failed.", GetLastError());
   }
}


template <size_t N, size_t M>
void CrPipe<N, M>::OnConnected()
{
   if (isConnectionPending_)
   {
      // Complete the connection process by getting the result of the connect operation.
      // We are not doing anything with the result but I suspect the system needs the
      // call to happen.
      DWORD numBytesProcessed = 0;
      const BOOL res = GetOverlappedResult(
         pipeHandle_.h, reinterpret_cast<OVERLAPPED*>(&info_), &numBytesProcessed, FALSE);
      assert(res && "Connection signaled as completed but GetOverlappedResult failed.");
      isConnectionPending_ = false;
   }

   isConnected_ = true;
   callbacks_.OnPipeConnected(*this);
}


template <size_t N, size_t M>
void CrPipe<N, M>::ListenForData()
{
   if (!isConnected_)
      return;

   if (!ReadFileEx(pipeHandle_.h, readBuffer_.data(), N,
                   reinterpret_cast<OVERLAPPED*>(&info_), ReadCompleted))
      Disconnect();
}


template <size_t N, size_t M>
void CrPipe<N, M>::SendData(const uint8_t* data, size_t dataSize)
{
   if (!isConnected_)
      return;

   assert(dataSize <= M);
   const size_t numBytesSending = std::min(dataSize, M);
   std::copy(data, data + numBytesSending, writeBuffer_.data());

   if (!WriteFileEx(pipeHandle_.h, writeBuffer_.data(),
                    static_cast<DWORD>(numBytesSending),
                    reinterpret_cast<OVERLAPPED*>(&info_), WriteCompleted))
      Disconnect();
}


template <size_t N, size_t M>
void CrPipe<N, M>::Disconnect()
{
   if (!DisconnectNamedPipe(pipeHandle_.h))
      throw Error("Win API DisconnectNamedPipe failed.", GetLastError());

   // Pipe instances delete themselves.
   delete this;
}


// static
template <size_t N, size_t M>
void WINAPI CrPipe<N, M>::ReadCompleted(DWORD err, DWORD numBytesRead, OVERLAPPED* pipeInfo)
{
   CrPipe<N, M>* self = reinterpret_cast<PipeInfo*>(pipeInfo)->self;
   assert(self);
   if (self)
      self->OnReadCompleted(err, numBytesRead);
}


// static
template <size_t N, size_t M>
void WINAPI CrPipe<N, M>::WriteCompleted(DWORD err, DWORD numBytesWritten,
                                      OVERLAPPED* pipeInfo)
{
   CrPipe<N, M>* self = reinterpret_cast<PipeInfo*>(pipeInfo)->self;
   assert(self);
   if (self)
      self->OnWriteCompleted(err, numBytesWritten);
}


template <size_t N, size_t M>
void CrPipe<N, M>::OnReadCompleted(DWORD err, DWORD numBytesRead)
{
   if (err != ERROR_SUCCESS)
      return Disconnect();

   // To determine whether the read buffer overflowed we have to first call
   // GetOverlappedResult and then GetLastError which will return ERROR_MORE_DATA in the
   // case of an overflow.
   DWORD numBytesProcessed = 0;
   GetOverlappedResult(pipeHandle_.h, reinterpret_cast<OVERLAPPED*>(&info_),
                       &numBytesProcessed, FALSE);
   const bool haveOverflow = (GetLastError() == ERROR_MORE_DATA);

   if (haveOverflow)
   {
      // Reset the error status to be able to detect an overflow for the next data chunk.
      SetLastError(ERROR_SUCCESS);
      // Callback should collect the partial data and listen for more data.
      callbacks_.OnPartialDataReceived(*this, readBuffer_.data(), numBytesRead);
   }
   else
   {
      callbacks_.OnDataReceived(*this, readBuffer_.data(), numBytesRead);
   }
}


template <size_t N, size_t M>
void CrPipe<N, M>::OnWriteCompleted(DWORD err, DWORD numBytesWritten)
{
   if (err != ERROR_SUCCESS)
      return Disconnect();
    
   callbacks_.OnDataSent(*this);
}


///////////////////

// Default implementation of CrPipeCallbacks

template <size_t N, size_t M>
void CrPipeCallbacks<N, M>::OnPipeConnected(CrPipe<N, M>& pipe)
{
   // Listen for data.
   // Derive and implement to change behavior.
   pipe.ListenForData();
}


template <size_t N, size_t M>
void CrPipeCallbacks<N, M>::OnDataReceived(CrPipe<N, M>& pipe, const uint8_t* data,
                                           size_t dataSize)
{
   // Derive and implement to process data and send a response by calling
   // CrPipe<N, M>::SendData on the passed pipe reference.
}


template <size_t N, size_t M>
void CrPipeCallbacks<N, M>::OnPartialDataReceived(CrPipe<N, M>& pipe, const uint8_t* data,
                                               size_t dataSize)
{
   // Derive and implement to collect the partial data.
   // Then listen for more data.
   pipe.ListenForData();
}


template <size_t N, size_t M>
void CrPipeCallbacks<N, M>::OnDataSent(CrPipe<N, M>& pipe)
{
   // Listen for more data.
   // Derive and implement to change behavior.
   pipe.ListenForData();
}


///////////////////

// Single-threaded pipe server with ansynchronously overlapping pipe operations. Completed
// pipe operations are signaled by an event and a subsequent call to an I/O completion
// routine.
// Cr - Completion Routine
template <size_t N = DefaultBufferCapacity, size_t M = N>
class CrPipeServer
{
public:
   explicit CrPipeServer(CrPipeCallbacks<N, M>& callbacks);
   ~CrPipeServer() = default;
   CrPipeServer(const CrPipeServer&) = delete;
   CrPipeServer(CrPipeServer&&) = default;
   CrPipeServer& operator=(const CrPipeServer&) = delete;
   CrPipeServer& operator=(CrPipeServer&&) = default;

   void SetPipeReadyEvent(HANDLE readyEvent);

   // Pipe name should be of form: \\\\.\\pipe\\<mypipename>
   void Run(const std::string& pipeName);

private:
   CrPipeCallbacks<N, M>& callbacks_;
   // Event shared between all pipes to signal completed operations and also to
   // signal newly connected clients.
   ManualResetEvent sharedEvent_;
   // Event set by the calling code to allow it to be notified when the pipe is ready.
   // Calling code is responsible for closing the handle.
   HANDLE pipeReadyEvent_ = NULL;
};



template <size_t N, size_t M>
CrPipeServer<N, M>::CrPipeServer(CrPipeCallbacks<N, M>& callbacks)
   : callbacks_(callbacks)
{
}


template <size_t N, size_t M>
void CrPipeServer<N, M>::SetPipeReadyEvent(HANDLE readyEvent)
{
   pipeReadyEvent_ = readyEvent;
}


template <size_t N, size_t M>
void CrPipeServer<N, M>::Run(const std::string& pipeName)
{
   CrPipe<N, M>* nextPipe = new CrPipe<N, M>(callbacks_);
   nextPipe->Connect(pipeName, sharedEvent_.Handle());

   // If requested notify the caller that the pipe is ready.
   if (pipeReadyEvent_)
      SetEvent(pipeReadyEvent_);

   while (true)
   {
      switch (WaitForSingleObjectEx(sharedEvent_.Handle(), INFINITE, TRUE))
      {
      case 0:
         // A client connected. Complete the connection process for the current pipe by
         // notifying it.
         nextPipe->OnConnected();
         // Create a new pipe to wait for the next client. This will basically cast the
         // just connected pipe out to live on its own. The cast out pipe will delete
         // itself eventually.
         nextPipe = new CrPipe<N, M>(callbacks_);
         nextPipe->Connect(pipeName, sharedEvent_.Handle());
         break;

      case WAIT_IO_COMPLETION:
         // An operation on one of the pipes completed. Nothing to do here. The completion
         // routine will be called by the system.
         break;

      default:
         throw Error("Unexpected result from Win API WaitForSingleObjectEx.",
                     GetLastError());
      }
   }
}


///////////////////

// Abstracts a buffer that gets passed to a client-side pipe when reading data.
struct PipeReadBuffer
{
   virtual void Put(const uint8_t* data, size_t dataSize) = 0;

protected:
   // No deleting through this interface.
   ~PipeReadBuffer() = default;
};


// Client-side pipe. Uses blocking operations to connect to pipe server as well as for
// reads and writes.
template <size_t N = DefaultBufferCapacity>
class ClientPipe
{
public:
   ClientPipe() = default;
   ~ClientPipe();
   ClientPipe(const ClientPipe&) = delete;
   ClientPipe(ClientPipe&&) = default;
   ClientPipe& operator=(const ClientPipe&) = delete;
   ClientPipe& operator=(ClientPipe&&) = default;

   static constexpr size_t InfiniteWait = -1;
   // Pipe name should be of form: \\\\.\\pipe\\<mypipename>
   bool Connect(const std::string& pipeName, size_t waitIntervalMs);
   void Disconnect();

   void SendData(const uint8_t* data, size_t dataSize) const;
   void WaitForData(PipeReadBuffer& buffer);

   explicit operator bool() const { return IsConnected(); }

private:
   bool IsConnected() const { return !!pipeHandle_; }
   bool ConnectPipeImmediately(const std::string& pipeName);
   bool WaitForPipe(const std::string& pipeName, size_t waitIntervalMs);

private:
   Handle pipeHandle_;
   std::array<uint8_t, N> readBuffer_;
};


template <size_t N>
ClientPipe<N>::~ClientPipe()
{
   if (pipeHandle_)
      Disconnect();
}


template <size_t N>
bool ClientPipe<N>::Connect(const std::string& pipeName, size_t waitIntervalMs)
{
   if (ConnectPipeImmediately(pipeName))
      return true;
   // Wait for a pipe to become available.
   if (WaitForPipe(pipeName.c_str(), waitIntervalMs))
      return ConnectPipeImmediately(pipeName);
   // Wait expired.
   return false;
}


template <size_t N>
void ClientPipe<N>::Disconnect()
{
   CloseHandle(pipeHandle_.h);
   pipeHandle_.h = NULL;
}


template <size_t N>
void ClientPipe<N>::SendData(const uint8_t* data, size_t dataSize) const
{
   assert(data);
   assert(IsConnected());
   if (!IsConnected())
      return;

   DWORD numBytesWritten = 0;
   if (!WriteFile(pipeHandle_.h, data, static_cast<DWORD>(dataSize), &numBytesWritten,
                  nullptr))
      throw Error("Failed to write to client-side pipe.", GetLastError());
}


template <size_t N>
void ClientPipe<N>::WaitForData(PipeReadBuffer& buffer)
{
   assert(IsConnected());
   if (!IsConnected())
      return;

   bool isFinished = false;
   DWORD numBytesRead = 0;
   do
   {
      isFinished = ReadFile(pipeHandle_.h, readBuffer_.data(), N, &numBytesRead, nullptr)
                      ? true
                      : false;
      if (!isFinished && GetLastError() != ERROR_MORE_DATA)
         throw Error("Failed to read from client-side pipe.", GetLastError());
      
      buffer.Put(readBuffer_.data(), numBytesRead);
   } while (!isFinished);
}


template <size_t N>
bool ClientPipe<N>::ConnectPipeImmediately(const std::string& pipeName)
{
   constexpr DWORD Sharing = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;
   constexpr DWORD Attributes = 0;
   pipeHandle_.h = CreateFile(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, Sharing,
                              nullptr, OPEN_EXISTING, Attributes, nullptr);
   
   // Check for error other than 'no pipe available'.
   if (pipeHandle_.h == INVALID_HANDLE_VALUE && GetLastError() != ERROR_PIPE_BUSY)
         throw Error("Failed to open client-side pipe.", GetLastError());
   
   if (IsConnected())
   {
      // Set to message mode.
      DWORD mode = PIPE_READMODE_MESSAGE;
      if (!SetNamedPipeHandleState(pipeHandle_.h, &mode, nullptr, nullptr))
         throw Error("Failed to set mode of client-side pipe.", GetLastError());
   }
   
   return IsConnected();
}


template <size_t N>
bool ClientPipe<N>::WaitForPipe(const std::string& pipeName, size_t waitIntervalMs)
{
   if (WaitNamedPipe(pipeName.c_str(), static_cast<DWORD>(waitIntervalMs)))
      return true;

   // Check for error other than 'timed out'.
   if (GetLastError() != ERROR_SEM_TIMEOUT)
      throw Error("Waiting for pipe failed.", GetLastError());
 
   return false;
}

} // namespace ipc
