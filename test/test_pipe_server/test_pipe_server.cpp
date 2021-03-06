#include "..\..\pipes.h"
#include "..\test_pipe.h"
#include "win.h"
#include <map>
#include <string>
#include <vector>

using namespace ipc;

namespace {

template <size_t ReadBufSize, size_t WriteBufSize>
struct EchoPipe : public CrPipeCallbacks<ReadBufSize, WriteBufSize>
{
   using ServerPipe_t = CrPipe<ReadBufSize, WriteBufSize>;
   using BaseCallbacks_t = CrPipeCallbacks<ReadBufSize, WriteBufSize>;

   void OnPipeConnected(ServerPipe_t& pipe) override;
   void OnDataReceived(ServerPipe_t& pipe, const uint8_t* data,
                       size_t dataSize) override;
   void OnPartialDataReceived(ServerPipe_t& pipe, const uint8_t* data,
                              size_t dataSize) override;
   void OnDataSent(ServerPipe_t& pipe) override;

private:
   // Maps pipes to partial data buffers.
   std::map<HANDLE, std::vector<uint8_t>> partialDataAccumulators_;
};


template <size_t ReadBufSize, size_t WriteBufSize>
void EchoPipe<ReadBufSize, WriteBufSize>::OnPipeConnected(ServerPipe_t& pipe)
{
   OutputDebugString("Pipe connected.\n");
   partialDataAccumulators_[pipe.PipeHandle()] = {};
   BaseCallbacks_t::OnPipeConnected(pipe);
}


template <size_t ReadBufSize, size_t WriteBufSize>
void EchoPipe<ReadBufSize, WriteBufSize>::OnDataReceived(ServerPipe_t& pipe,
                                                         const uint8_t* data,
                                                         size_t dataSize)
{
   OutputDebugString("Data received.\n");

   const uint8_t* completeData = data;
   size_t completeDataSize = dataSize;

   // If we have partial data accumulated, then this is the last data chunk in a sequence.
   // We have to add the last chunk and then process the complete data.
   auto partialDataIter = partialDataAccumulators_.find(pipe.PipeHandle());
   const bool havePartialData = (partialDataIter != partialDataAccumulators_.end() &&
                                 !partialDataIter->second.empty());
   if (havePartialData)
   {
      std::copy(data, data + dataSize, std::back_inserter(partialDataIter->second));
      completeData = partialDataIter->second.data();
      completeDataSize = partialDataIter->second.size();
   }

   // Process the received data.
   std::string response = "Pipe server received data: ";
   if (completeDataSize > 0)
   {
      response.append(reinterpret_cast<const char*>(completeData),
                      reinterpret_cast<const char*>(completeData) + completeDataSize);
      response.append("\0");
   }
   else
   {
      response += "<none>";
   }

   // Limit response size to write buffer size.
   if (response.size() > WriteBufSize)
      response.resize(WriteBufSize);

   // Clear the partial data for the processed pipe.
   if (havePartialData)
      partialDataIter->second.clear();

   pipe.SendData(reinterpret_cast<const uint8_t*>(response.data()), response.size());
}


template <size_t ReadBufSize, size_t WriteBufSize>
void EchoPipe<ReadBufSize, WriteBufSize>::OnPartialDataReceived(ServerPipe_t& pipe,
                                                                const uint8_t* data,
                                                                size_t dataSize)
{
   OutputDebugString("Partial data received.\n");

   // Collect the partial data in the accumulator for the pipe.
   std::copy(data, data + dataSize,
             std::back_inserter(partialDataAccumulators_[pipe.PipeHandle()]));
   // Call base to listen for more data.
   BaseCallbacks_t::OnPartialDataReceived(pipe, data, dataSize);
}


template <size_t ReadBufSize, size_t WriteBufSize>
void EchoPipe<ReadBufSize, WriteBufSize>::OnDataSent(ServerPipe_t& pipe)
{
   OutputDebugString("Data sent.\n");
   BaseCallbacks_t::OnDataSent(pipe);
}


///////////////////

template <size_t ReadBufSize, size_t WriteBufSize>
struct PipeTest
{
   void Run()
   {
      EchoPipe<ReadBufSize, WriteBufSize> echo;
      CrPipeServer<ReadBufSize, WriteBufSize> server(echo);
      server.Run(test::PipeName.c_str());
   }
};

} // namespace


int main(int argc, char* argv[])
{
   const bool runSmallBufferTest = (argc > 1 && std::string(argv[1]) == "smallbuffer");
   if (runSmallBufferTest)
   {
      // Small buffer test for the server's read buffer. Needs to handle buffer overflows.
      // Note that handling a small write buffer is easier, the data copied into the
      // buffer is simply truncated.
      PipeTest<test::SmallPipeBufferSize, test::LargePipeBufferSize>().Run();
   }
   else
   {
      PipeTest<test::LargePipeBufferSize, test::LargePipeBufferSize>().Run();
   }

   return 0;

}

