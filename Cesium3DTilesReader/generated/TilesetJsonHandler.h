// This file was generated by generate-classes.
// DO NOT EDIT THIS FILE!
#pragma once

#include "AssetJsonHandler.h"
#include "PropertiesJsonHandler.h"
#include "TileJsonHandler.h"

#include <Cesium3DTiles/Tileset.h>
#include <CesiumJsonReader/ArrayJsonHandler.h>
#include <CesiumJsonReader/DictionaryJsonHandler.h>
#include <CesiumJsonReader/DoubleJsonHandler.h>
#include <CesiumJsonReader/ExtensibleObjectJsonHandler.h>
#include <CesiumJsonReader/StringJsonHandler.h>

namespace CesiumJsonReader {
class ExtensionReaderContext;
}

namespace Cesium3DTiles {
class TilesetJsonHandler
    : public CesiumJsonReader::ExtensibleObjectJsonHandler {
public:
  using ValueType = Tileset;

  TilesetJsonHandler(
      const CesiumJsonReader::ExtensionReaderContext& context) noexcept;
  void reset(IJsonHandler* pParentHandler, Tileset* pObject);

  virtual IJsonHandler* readObjectKey(const std::string_view& str) override;

protected:
  IJsonHandler* readObjectKeyTileset(
      const std::string& objectType,
      const std::string_view& str,
      Tileset& o);

private:
  Tileset* _pObject = nullptr;
  AssetJsonHandler _asset;
  CesiumJsonReader::DictionaryJsonHandler<Properties, PropertiesJsonHandler>
      _properties;
  CesiumJsonReader::DoubleJsonHandler _geometricError;
  TileJsonHandler _root;
  CesiumJsonReader::
      ArrayJsonHandler<std::string, CesiumJsonReader::StringJsonHandler>
          _extensionsUsed;
  CesiumJsonReader::
      ArrayJsonHandler<std::string, CesiumJsonReader::StringJsonHandler>
          _extensionsRequired;
};
} // namespace Cesium3DTiles
