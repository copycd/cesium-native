// This file was generated by generate-classes.
// DO NOT EDIT THIS FILE!
#pragma once

#include "NamedObjectJsonHandler.h"

#include <CesiumGltf/Buffer.h>
#include <CesiumJsonReader/IntegerJsonHandler.h>
#include <CesiumJsonReader/StringJsonHandler.h>

namespace CesiumJsonReader {
class ExtensionReaderContext;
}

namespace CesiumGltf {
class BufferJsonHandler : public NamedObjectJsonHandler {
public:
  using ValueType = Buffer;

  BufferJsonHandler(
      const CesiumJsonReader::ExtensionReaderContext& context) noexcept;
  void reset(IJsonHandler* pParentHandler, Buffer* pObject);

  virtual IJsonHandler* readObjectKey(const std::string_view& str) override;

protected:
  IJsonHandler* readObjectKeyBuffer(
      const std::string& objectType,
      const std::string_view& str,
      Buffer& o);

private:
  Buffer* _pObject = nullptr;
  CesiumJsonReader::StringJsonHandler _uri;
  CesiumJsonReader::IntegerJsonHandler<int64_t> _byteLength;
};
} // namespace CesiumGltf
