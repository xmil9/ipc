#pragma once
#include <stdexcept>
#include <string>


namespace ipc {

///////////////////

struct Error : public std::runtime_error
{
   Error(const char* text) : std::runtime_error(text) {}
   // Appends '\nError code: <err>.' to passed text.
   Error(const char* text, int err) : std::runtime_error(FormatText(text, err)) {}

   static std::string FormatText(const char* text, int err);
};


inline std::string Error::FormatText(const char* text, int err)
{
   std::string fullText = text;
   fullText += "\nError code: ";
   fullText += std::to_string(err);
   fullText += ".";
   return fullText;
}

} //namespace ipc
