#include "..\..\pipes.h"
#include "..\test_pipe.h"
#include "win.h"
#include <memory>
#include <vector>

using namespace ipc;


namespace {

///////////////////

struct PipeBuffer : public PipeReadBuffer
{
   PipeBuffer() = default;

   void Put(const uint8_t* data, size_t dataSize) override
   {
      std::copy(data, data + dataSize, std::back_inserter(buffer));
   }

   std::vector<uint8_t> buffer;
};


///////////////////

template <size_t BufSize>
struct PipeTest
{
   void Run()
   {
      constexpr size_t ConnectionTimeoutMs = 100000;
      const std::string pidStr = std::to_string(GetCurrentProcessId());
      const std::string msgTitle = "Pipe client " + pidStr;

      try
      {
         ClientPipe<BufSize> pipe;
         if (pipe.Connect(test::PipeName.c_str(), ConnectionTimeoutMs))
         {
            // Use a data string that is longer than the small buffer size to force
            // overflows for small buffer test on the server-side.
            std::string data = "Client " + pidStr + " is sending this string as data.";
            pipe.SendData(reinterpret_cast<const uint8_t*>(data.data()), data.size());

            PipeBuffer response;
            pipe.WaitForData(response);
            response.buffer.push_back('\0');

            std::string confirmation = "Response: ";
            confirmation.append(reinterpret_cast<const char*>(response.buffer.data()));
            MessageBox(NULL, confirmation.c_str(), msgTitle.c_str(), MB_OK);

            pipe.Disconnect();
         }
      }
      catch (std::runtime_error& ex)
      {
         std::string errDescription = "Error: ";
         errDescription += ex.what();
         MessageBox(NULL, errDescription.c_str(), msgTitle.c_str(), MB_OK);
      }
   }
};

} // namespace


int main(int argc, char* argv[])
{
   const bool runSmallBufferTest = (argc > 1 && std::string(argv[1]) == "smallbuffer");
   if (runSmallBufferTest)
      PipeTest<test::SmallPipeBufferSize>().Run();
   else
      PipeTest<test::LargePipeBufferSize>().Run();
 
   return 0;
}

