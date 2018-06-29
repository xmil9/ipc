#pragma once
#include "error.h"
#include "sfinae_util.h"
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>


namespace ipc {

///////////////////

// Abstracts placing data into a buffer.
struct SerializationBuffer
{
   virtual void Put(const uint8_t* data, size_t dataSize) = 0;

protected:
   // No deleting through this interface.
   ~SerializationBuffer() = default;
};


// Serializes all types except for std::basic_string types.
template <typename T>
void serialize(const T& val, SerializationBuffer& target)
{
   target.Put(reinterpret_cast<const uint8_t*>(&val), sizeof(val));
}


// Serializes std::basic_string types.
template <typename TChar>
void serialize(const std::basic_string<TChar>& str, SerializationBuffer& target)
{
   // Store in size_t variable first and serialize that variable to make sure we know
   // what kind of value to deserialize.
   const size_t len = str.length();
   serialize(len + 1, target);
   target.Put(reinterpret_cast<const uint8_t*>(str.data()), len * sizeof(TChar));
   serialize<TChar>(0, target);
}


///////////////////

// Abstracts taking data out of a buffer.
struct DeserializationBuffer
{
   virtual const uint8_t* Take(size_t dataSize) = 0;

protected:
   // No deleting through this interface.
   ~DeserializationBuffer() = default;
};


// Deserializes all types except for std::basic_string types.
template <typename T>
typename std::enable_if<!is_string<T>::value, T>::type
deserialize(DeserializationBuffer& source)
{
   return *reinterpret_cast<const T*>(source.Take(sizeof(T)));
}


// Deserializes std::basic_string types.
template <typename T>
typename std::enable_if<is_string<T>::value, T>::type
deserialize(DeserializationBuffer& source)
{
   using Char_t = typename T::value_type;
   // Deserialize the string's length.
   const size_t lenPlusTerminator = deserialize<size_t>(source);
   return reinterpret_cast<const Char_t*>(source.Take(lenPlusTerminator));
}


///////////////////

// Adapts a std::vector to the SerializationBuffer interface.
struct VectorSerializationBufferAdapter : public SerializationBuffer
{
   explicit VectorSerializationBufferAdapter(std::vector<uint8_t>& buffer)
   : buffer_(buffer) {}

   void Put(const uint8_t* data, size_t dataSize) override;

   std::vector<uint8_t>& buffer_;
};


inline void VectorSerializationBufferAdapter::Put(const uint8_t* data, size_t dataSize)
{
   std::copy(data, data + dataSize, std::back_inserter(buffer_));
}


///////////////////

// Adapts a std::vector to the DeserializationBuffer interface.
struct VectorDeserializationBufferAdapter : public DeserializationBuffer
{
   explicit VectorDeserializationBufferAdapter(std::vector<uint8_t>& buffer)
   : buffer_(buffer), pos_(0) {}
   VectorDeserializationBufferAdapter(std::vector<uint8_t>& buffer, size_t pos)
   : buffer_(buffer), pos_(pos) {}

   const uint8_t* Take(size_t dataSize) override;

   std::vector<uint8_t>& buffer_;
   size_t pos_;
};


inline const uint8_t* VectorDeserializationBufferAdapter::Take(size_t dataSize)
{
   if (dataSize == 0)
      return nullptr;
   if (dataSize > buffer_.size() - pos_)
      throw Error("Data of requested size not available.");

   const uint8_t* taken = buffer_.data() + pos_;
   pos_ += dataSize;
   return taken;
}

} // namespace ipc
