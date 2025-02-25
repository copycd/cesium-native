// This file was generated by generate-classes.
// DO NOT EDIT THIS FILE!
#pragma once

#include "FeatureTablePropertyJsonHandler.h"

#include <CesiumGltf/FeatureTable.h>
#include <CesiumJsonReader/DictionaryJsonHandler.h>
#include <CesiumJsonReader/ExtensibleObjectJsonHandler.h>
#include <CesiumJsonReader/IntegerJsonHandler.h>
#include <CesiumJsonReader/StringJsonHandler.h>

namespace CesiumJsonReader {
class ExtensionReaderContext;
}

namespace CesiumGltf {
class FeatureTableJsonHandler
    : public CesiumJsonReader::ExtensibleObjectJsonHandler {
public:
  using ValueType = FeatureTable;

  FeatureTableJsonHandler(
      const CesiumJsonReader::ExtensionReaderContext& context) noexcept;
  void reset(IJsonHandler* pParentHandler, FeatureTable* pObject);

  virtual IJsonHandler* readObjectKey(const std::string_view& str) override;

protected:
  IJsonHandler* readObjectKeyFeatureTable(
      const std::string& objectType,
      const std::string_view& str,
      FeatureTable& o);

private:
  FeatureTable* _pObject = nullptr;
  CesiumJsonReader::StringJsonHandler _classProperty;
  CesiumJsonReader::IntegerJsonHandler<int64_t> _count;
  CesiumJsonReader::DictionaryJsonHandler<
      FeatureTableProperty,
      FeatureTablePropertyJsonHandler>
      _properties;
};
} // namespace CesiumGltf
