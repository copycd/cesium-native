#include "Cesium3DTiles/Cesium3DTilesWriter.h"

#include "TilesetWriter.h"
#include "CesiumUtility/Tracing.h"

#include <CesiumJsonWriter/JsonWriter.h>
#include <CesiumJsonWriter/PrettyJsonWriter.h>

using namespace Cesium3DTiles;
using namespace CesiumJsonWriter;
using namespace CesiumUtility;

Cesium3DTilesWriter::Cesium3DTilesWriter() {}

CesiumJsonWriter::ExtensionWriterContext& Cesium3DTilesWriter::getExtensions() {
  return this->_context;
}

const CesiumJsonWriter::ExtensionWriterContext&
Cesium3DTilesWriter::getExtensions() const {
  return this->_context;
}

Cesium3DTilesWriterResult Cesium3DTilesWriter::writeTileset(
    const Tileset& tileset,
    const WriteTilesetOptions& options) const {
  CESIUM_TRACE("CesiumGltf::Cesium3DTilesWriter::writeTileset");

  const CesiumJsonWriter::ExtensionWriterContext& context =
      this->getExtensions();

  Cesium3DTilesWriterResult result;
  std::unique_ptr<CesiumJsonWriter::JsonWriter> writer;

  if (options.prettyPrint) {
    writer = std::make_unique<CesiumJsonWriter::PrettyJsonWriter>();
  } else {
    writer = std::make_unique<CesiumJsonWriter::JsonWriter>();
  }

  TilesetWriter::write(tileset, *writer, context);
  result.tilesetBytes = writer->toBytes();

  return result;
}