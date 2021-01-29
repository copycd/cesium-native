#include "CesiumAsync/DiskCache.h"
#include "CesiumAsync/IAssetResponse.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include <utility>
#include <stdexcept>

namespace CesiumAsync {
	struct Sqlite3StmtWrapper {
		Sqlite3StmtWrapper() 
			: stmt{nullptr}
		{}

		~Sqlite3StmtWrapper() {
			if (stmt) {
				sqlite3_finalize(stmt);
			}
		}

		sqlite3_stmt *stmt;
	};

	static const std::string CACHE_TABLE = "CacheItemTable";
	static const std::string CACHE_TABLE_ID_COLUMN = "id";
	static const std::string CACHE_TABLE_EXPIRY_TIME_COLUMN = "expiryTime";
	static const std::string CACHE_TABLE_LAST_ACCESSED_TIME_COLUMN = "lastAccessedTime";
	static const std::string CACHE_TABLE_RESPONSE_HEADER_COLUMN = "responseHeaders";
	static const std::string CACHE_TABLE_RESPONSE_CONTENT_TYPE_COLUMN = "responseContentType";
	static const std::string CACHE_TABLE_RESPONSE_STATUS_CODE_COLUMN = "responseStatusCode";
	static const std::string CACHE_TABLE_RESPONSE_CACHE_CONTROL_COLUMN = "responseCacheControl";
	static const std::string CACHE_TABLE_RESPONSE_DATA_COLUMN = "responseData";
	static const std::string CACHE_TABLE_REQUEST_HEADER_COLUMN = "requestHeader";
	static const std::string CACHE_TABLE_REQUEST_METHOD_COLUMN = "requestMethod";
	static const std::string CACHE_TABLE_REQUEST_URL_COLUMN = "requestUrl";
	static const std::string CACHE_TABLE_KEY_COLUMN = "key";
	static const std::string CACHE_TABLE_VIRTUAL_TOTAL_ITEMS_COLUMN = "totalItems";

	static std::string convertHeadersToString(const HttpHeaders& headers);

	static std::string convertCacheControlToString(const ResponseCacheControl* cacheControl);

	static HttpHeaders convertStringToHeaders(const std::string& serializedHeaders);

	static std::optional<ResponseCacheControl> convertStringToResponseCacheControl(const std::string& serializedResponseCacheControl);

	DiskCache::DiskCache(const std::string &databaseName, uint64_t maxItems)
		: _pConnection{nullptr}
		, _maxItems{maxItems}
	{
		int status = sqlite3_open(databaseName.c_str(), &this->_pConnection);
		if (status != SQLITE_OK) {
			throw std::runtime_error(sqlite3_errstr(status));
		}

		// create cache tables if not exist. Key -> Cache table: one-to-many relationship
		std::string createCacheTableSqlStr = "CREATE TABLE IF NOT EXISTS " + CACHE_TABLE + "(" +
			CACHE_TABLE_ID_COLUMN + " INTEGER PRIMARY KEY NOT NULL," +
			CACHE_TABLE_EXPIRY_TIME_COLUMN + " DATETIME NOT NULL," +
			CACHE_TABLE_LAST_ACCESSED_TIME_COLUMN + " DATETIME NOT NULL," +
			CACHE_TABLE_RESPONSE_HEADER_COLUMN + " TEXT NOT NULL," +
			CACHE_TABLE_RESPONSE_CONTENT_TYPE_COLUMN + " TEXT NOT NULL," +
			CACHE_TABLE_RESPONSE_STATUS_CODE_COLUMN + " INTEGER NOT NULL," +
			CACHE_TABLE_RESPONSE_CACHE_CONTROL_COLUMN + " TEXT NOT NULL," +
			CACHE_TABLE_RESPONSE_DATA_COLUMN + " BLOB," +
			CACHE_TABLE_REQUEST_HEADER_COLUMN + " TEXT NOT NULL," +
			CACHE_TABLE_REQUEST_METHOD_COLUMN + " TEXT NOT NULL," +
			CACHE_TABLE_REQUEST_URL_COLUMN + " TEXT NOT NULL," +
			CACHE_TABLE_KEY_COLUMN + " TEXT NOT NULL)";
		char* createTableError = nullptr;
		status = sqlite3_exec(this->_pConnection, createCacheTableSqlStr.c_str(), nullptr, nullptr, &createTableError);
		if (status != SQLITE_OK) {
			std::string errorStr(createTableError);
			sqlite3_free(createTableError);
			throw std::runtime_error(errorStr);
		}

		// index Key column
		std::string indexKeySqlStr = "CREATE INDEX IF NOT EXISTS " + CACHE_TABLE_KEY_COLUMN + "_index ON " + CACHE_TABLE + "(" + CACHE_TABLE_KEY_COLUMN + ")";
		char* indexKeyError = nullptr;
		status = sqlite3_exec(this->_pConnection, indexKeySqlStr.c_str(), nullptr, nullptr, &indexKeyError);
		if (status != SQLITE_OK) {
			std::string errorStr(indexKeyError);
			sqlite3_free(indexKeyError);
			throw std::runtime_error(errorStr);
		}
		
		// turn on WAL mode
		char* walError = nullptr;
		status = sqlite3_exec(this->_pConnection, "PRAGMA journal_mode=WAL", nullptr, nullptr, &walError);
		if (status != SQLITE_OK) {
			std::string errorStr(walError);
			sqlite3_free(walError);
			throw std::runtime_error(errorStr);
		}

		sqlite3_busy_timeout(this->_pConnection, 5000);
	}

	DiskCache::DiskCache(DiskCache&& other) noexcept {
		this->_pConnection = other._pConnection;
		this->_maxItems = other._maxItems;

		other._pConnection = nullptr;
		other._maxItems = 0;
	}

	DiskCache& DiskCache::operator=(DiskCache&& other) noexcept {
		if (&other != this) {
			this->_pConnection = other._pConnection;
			this->_maxItems = other._maxItems;

			other._pConnection = nullptr;
			other._maxItems = 0;
		}

		return *this;
	}

	DiskCache::~DiskCache() noexcept {
		if (this->_pConnection) {
			sqlite3_close(this->_pConnection);
		}
	}

	bool DiskCache::getEntry(const std::string& key, 
			std::function<bool(CacheItem)> predicate, 
			std::string& error) const 
	{
		// get entry based on key
		std::string selectSqlStr = "SELECT " +
			CACHE_TABLE_ID_COLUMN + ", " +
			CACHE_TABLE_EXPIRY_TIME_COLUMN + ", " +
			CACHE_TABLE_LAST_ACCESSED_TIME_COLUMN + ", " +
			CACHE_TABLE_RESPONSE_HEADER_COLUMN + ", " +
			CACHE_TABLE_RESPONSE_CONTENT_TYPE_COLUMN + " , " +
			CACHE_TABLE_RESPONSE_STATUS_CODE_COLUMN + ", " +
			CACHE_TABLE_RESPONSE_CACHE_CONTROL_COLUMN + ", " +
			CACHE_TABLE_RESPONSE_DATA_COLUMN + ", " +
			CACHE_TABLE_REQUEST_HEADER_COLUMN + ", " +
			CACHE_TABLE_REQUEST_METHOD_COLUMN + ", " +
			CACHE_TABLE_REQUEST_URL_COLUMN +
			" FROM " + CACHE_TABLE + " WHERE " + CACHE_TABLE_KEY_COLUMN + "=?";

		Sqlite3StmtWrapper selectStmtWrapper;
		int status = sqlite3_prepare_v2(_pConnection, selectSqlStr.c_str(), -1, &selectStmtWrapper.stmt, nullptr);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_bind_text(selectStmtWrapper.stmt, 1, key.c_str(), -1, SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		while ((status = sqlite3_step(selectStmtWrapper.stmt)) == SQLITE_ROW) {
			int64_t itemIndex = sqlite3_column_int64(selectStmtWrapper.stmt, 0);

			// parse cache item metadata
			std::time_t expiryTime = sqlite3_column_int64(selectStmtWrapper.stmt, 1);

			std::time_t lastAccessedTime = sqlite3_column_int64(selectStmtWrapper.stmt, 2);

			// parse response cache 
			std::string serializedResponseHeaders = reinterpret_cast<const char*>(sqlite3_column_text(selectStmtWrapper.stmt, 3));
			HttpHeaders responseHeaders = convertStringToHeaders(serializedResponseHeaders);

			std::string responseContentType = reinterpret_cast<const char*>(sqlite3_column_text(selectStmtWrapper.stmt, 4));

			uint16_t statusCode = static_cast<uint16_t>(sqlite3_column_int(selectStmtWrapper.stmt, 5));

			std::string serializedResponseCacheControl = reinterpret_cast<const char*>(sqlite3_column_text(selectStmtWrapper.stmt, 6));
			std::optional<ResponseCacheControl> responseCacheControl = convertStringToResponseCacheControl(serializedResponseCacheControl);

			const void* rawResponseData = sqlite3_column_blob(selectStmtWrapper.stmt, 7);
			int responseDataSize = sqlite3_column_bytes(selectStmtWrapper.stmt, 7);
			std::vector<uint8_t> responseData(responseDataSize);
			if (responseDataSize != 0) {
				std::memcpy(responseData.data(), rawResponseData, responseDataSize);
			}

			CacheResponse cacheResponse(statusCode, 
				std::move(responseContentType), 
				std::move(responseHeaders), 
				std::move(responseCacheControl), 
				std::move(responseData));
			
			// parse request
			std::string serializedRequestHeaders = reinterpret_cast<const char*>(sqlite3_column_text(selectStmtWrapper.stmt, 8));
			HttpHeaders requestHeaders = convertStringToHeaders(serializedRequestHeaders);

			std::string requestMethod = reinterpret_cast<const char*>(sqlite3_column_text(selectStmtWrapper.stmt, 9));

			std::string requestUrl = reinterpret_cast<const char*>(sqlite3_column_text(selectStmtWrapper.stmt, 10));

			CacheRequest cacheRequest(std::move(requestHeaders), 
				std::move(requestMethod), 
				std::move(requestUrl));

			CacheItem item{ expiryTime,
				lastAccessedTime,
				std::move(cacheRequest),
				std::move(cacheResponse) };

			if (predicate(std::move(item))) {
				// update the last accessed time
				std::string updateSqlStr = "UPDATE " + CACHE_TABLE + 
					" SET " + CACHE_TABLE_LAST_ACCESSED_TIME_COLUMN + " = strftime('%s','now') WHERE " + CACHE_TABLE_KEY_COLUMN + " =?";
				Sqlite3StmtWrapper updateStmtWrapper;
				int updateStatus = sqlite3_prepare_v2(this->_pConnection, updateSqlStr.c_str(), -1, &updateStmtWrapper.stmt, nullptr);
				if (updateStatus != SQLITE_OK) {
					error = std::string(sqlite3_errstr(status));
					return false;
				}

				updateStatus = sqlite3_bind_int64(updateStmtWrapper.stmt, 1, itemIndex);
				if (updateStatus != SQLITE_OK) {
					error = std::string(sqlite3_errstr(status));
					return false;
				}

				updateStatus = sqlite3_step(updateStmtWrapper.stmt);
				if (updateStatus != SQLITE_DONE) {
					error = std::string(sqlite3_errstr(status));
					return false;
				}

				break;
			}
		}

		if (status != SQLITE_DONE && status != SQLITE_ROW) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		return true;
	}

	bool DiskCache::storeResponse(const std::string& key, 
		std::time_t expiryTime,
		const IAssetRequest& request,
		std::string& error) 
	{
		const IAssetResponse* response = request.response();
		if (response == nullptr) {
			error = std::string("Request needs to have a response");
			return false;
		}

		gsl::span<const uint8_t> responseData = response->data();

		// cache the request with the key
		Sqlite3StmtWrapper replaceStmtWrapper;
		std::string replaceCacheSqlStr = "REPLACE INTO " + CACHE_TABLE + " (" +
			CACHE_TABLE_EXPIRY_TIME_COLUMN + ", " +
			CACHE_TABLE_LAST_ACCESSED_TIME_COLUMN + ", " +
			CACHE_TABLE_RESPONSE_HEADER_COLUMN + ", " +
			CACHE_TABLE_RESPONSE_CONTENT_TYPE_COLUMN + " , " +
			CACHE_TABLE_RESPONSE_STATUS_CODE_COLUMN + ", " +
			CACHE_TABLE_RESPONSE_CACHE_CONTROL_COLUMN + ", " +
			CACHE_TABLE_RESPONSE_DATA_COLUMN + ", " +
			CACHE_TABLE_REQUEST_HEADER_COLUMN + ", " +
			CACHE_TABLE_REQUEST_METHOD_COLUMN + ", " +
			CACHE_TABLE_REQUEST_URL_COLUMN + ", " +
			CACHE_TABLE_KEY_COLUMN +
			") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
		int status = sqlite3_prepare_v2(this->_pConnection, replaceCacheSqlStr.c_str(), -1, &replaceStmtWrapper.stmt, nullptr);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_bind_int64(replaceStmtWrapper.stmt, 1, static_cast<int64_t>(expiryTime));
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_bind_int64(replaceStmtWrapper.stmt, 2, static_cast<int64_t>(std::time(0)));
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		std::string responseHeader = convertHeadersToString(response->headers());
		status = sqlite3_bind_text(replaceStmtWrapper.stmt, 3, responseHeader.c_str(), -1, SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}
		
		const std::string& responseContentType = response->contentType();
		status = sqlite3_bind_text(replaceStmtWrapper.stmt, 4, responseContentType.c_str(), -1, SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_bind_int(replaceStmtWrapper.stmt, 5, static_cast<int>(response->statusCode()));
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		std::string responseCacheControl = convertCacheControlToString(response->cacheControl());
		status = sqlite3_bind_text(replaceStmtWrapper.stmt, 6, responseCacheControl.c_str(), -1, SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_bind_blob(replaceStmtWrapper.stmt, 7, responseData.data(), static_cast<int>(responseData.size()), SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		std::string requestHeader = convertHeadersToString(request.headers());
		status = sqlite3_bind_text(replaceStmtWrapper.stmt, 8, requestHeader.c_str(), -1, SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		const std::string& requestMethod = request.method();
		status = sqlite3_bind_text(replaceStmtWrapper.stmt, 9, requestMethod.c_str(), -1, SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		const std::string& requestUrl = request.url();
		status = sqlite3_bind_text(replaceStmtWrapper.stmt, 10, requestUrl.c_str(), -1, SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_bind_text(replaceStmtWrapper.stmt, 11, key.c_str(), -1, SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_step(replaceStmtWrapper.stmt);
		if (status != SQLITE_DONE) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		return true;
	}

	bool DiskCache::removeEntry(const std::string& key, std::string& error) 
	{
		std::string sqlStr = "DELETE FROM " + CACHE_TABLE + " WHERE " + CACHE_TABLE_KEY_COLUMN + "=?";

		Sqlite3StmtWrapper removeStmtWrapper;
		int status = sqlite3_prepare_v2(this->_pConnection, sqlStr.c_str(), -1, &removeStmtWrapper.stmt, nullptr);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_bind_text(removeStmtWrapper.stmt, 1, key.c_str(), -1, SQLITE_STATIC);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_step(removeStmtWrapper.stmt);
		if (status != SQLITE_DONE) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		return true;
	}

	bool DiskCache::prune(std::string& error)
	{
		// query total size of response's data
		std::string totalItemsQuerySqlStr = "SELECT COUNT(*) " + CACHE_TABLE_VIRTUAL_TOTAL_ITEMS_COLUMN + " FROM " + CACHE_TABLE;
		Sqlite3StmtWrapper totalItemsQueryStmtWrapper;
		int status = sqlite3_prepare_v2(this->_pConnection, totalItemsQuerySqlStr.c_str(), -1, &totalItemsQueryStmtWrapper.stmt, nullptr);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_step(totalItemsQueryStmtWrapper.stmt);
		if (status == SQLITE_DONE) {
			return true;
		}
		else if (status != SQLITE_ROW) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		// prune the rows if over maximum
		int64_t totalItems = sqlite3_column_int64(totalItemsQueryStmtWrapper.stmt, 0);
		if (totalItems > 0 && totalItems <= static_cast<int64_t>(_maxItems)) {
			return true;
		}

		// delete expired rows first
		Sqlite3StmtWrapper deleteExpiredStmtWrapper;
		std::string deleteExpiredSqlStr = "DELETE FROM " + CACHE_TABLE + " WHERE " + CACHE_TABLE_EXPIRY_TIME_COLUMN + " < strftime('%s','now')";
		int deleteExpiredStatus = sqlite3_prepare_v2(this->_pConnection, deleteExpiredSqlStr.c_str(), -1, &deleteExpiredStmtWrapper.stmt, nullptr);
		if (deleteExpiredStatus != SQLITE_OK) {
			error = std::string(sqlite3_errstr(deleteExpiredStatus));
			return false;
		}

		deleteExpiredStatus = sqlite3_step(deleteExpiredStmtWrapper.stmt);
		if (deleteExpiredStatus != SQLITE_DONE) {
			error = std::string(sqlite3_errstr(deleteExpiredStatus));
			return false;
		}

		// check if we should delete more
		int deletedRows = sqlite3_changes(this->_pConnection);
		if (totalItems - deletedRows < static_cast<int>(this->_maxItems)) {
			return true;
		}

		totalItems -= deletedRows;

		// delete rows LRU if we are still over maximum
		Sqlite3StmtWrapper LRUDeleteStmtWrapper;
		std::string LRUDeleteSqlStr = "DELETE FROM " + CACHE_TABLE + " WHERE " + CACHE_TABLE_ID_COLUMN + 
			" IN (SELECT " + CACHE_TABLE_ID_COLUMN + " FROM " + CACHE_TABLE + " ORDER BY " + CACHE_TABLE_LAST_ACCESSED_TIME_COLUMN + " ASC " + " LIMIT " + std::to_string(totalItems - this->_maxItems)  + ")";
		int LRUDeleteStatus = sqlite3_prepare_v2(this->_pConnection, LRUDeleteSqlStr.c_str(), -1, &LRUDeleteStmtWrapper.stmt, nullptr);
		if (LRUDeleteStatus != SQLITE_OK) {
			error = std::string(sqlite3_errstr(LRUDeleteStatus));
			return false;
		}

		LRUDeleteStatus = sqlite3_step(LRUDeleteStmtWrapper.stmt);
		if (LRUDeleteStatus != SQLITE_DONE) {
			error = std::string(sqlite3_errstr(LRUDeleteStatus));
			return false;
		}

		return true;
	}

	bool DiskCache::clearAll(std::string& error)
	{
		std::string sqlStr = "DELETE FROM " + CACHE_TABLE;

		Sqlite3StmtWrapper deleteStmtWrapper;
		int status = sqlite3_prepare_v2(this->_pConnection, sqlStr.c_str(), -1, &deleteStmtWrapper.stmt, nullptr);
		if (status != SQLITE_OK) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		status = sqlite3_step(deleteStmtWrapper.stmt);
		if (status != SQLITE_DONE) {
			error = std::string(sqlite3_errstr(status));
			return false;
		}

		return true;
	}

	std::string convertHeadersToString(const HttpHeaders& headers) {
		rapidjson::Document document;
		rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
		rapidjson::Value root(rapidjson::kObjectType);
		rapidjson::Value key(rapidjson::kStringType);
		rapidjson::Value value(rapidjson::kStringType);
		for (const std::pair<std::string, std::string>& header : headers) 
		{
			key.SetString(header.first.c_str(), allocator);
			value.SetString(header.second.c_str(), allocator);
			root.AddMember(key, value, allocator);
		}

		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		root.Accept(writer);
		return buffer.GetString();
	}

	std::string convertCacheControlToString(const ResponseCacheControl* cacheControl) {
		if (!cacheControl) {
			return "";
		}

		rapidjson::Document document;
		rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
		rapidjson::Value root(rapidjson::kObjectType);
		root.AddMember("mustRevalidate", rapidjson::Value(cacheControl->mustRevalidate()), allocator);
		root.AddMember("noCache", rapidjson::Value(cacheControl->noCache()), allocator);
		root.AddMember("noStore", rapidjson::Value(cacheControl->noStore()), allocator);
		root.AddMember("noTransform", rapidjson::Value(cacheControl->noTransform()), allocator);
		root.AddMember("accessControlPublic", rapidjson::Value(cacheControl->accessControlPublic()), allocator);
		root.AddMember("accessControlPrivate", rapidjson::Value(cacheControl->accessControlPrivate()), allocator);
		root.AddMember("proxyRevalidate", rapidjson::Value(cacheControl->proxyRevalidate()), allocator);
		root.AddMember("maxAge", rapidjson::Value(cacheControl->maxAge()), allocator);
		root.AddMember("sharedMaxAge", rapidjson::Value(cacheControl->sharedMaxAge()), allocator);

		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		root.Accept(writer);
		return buffer.GetString();
	}

	HttpHeaders convertStringToHeaders(const std::string& serializedHeaders) {
		rapidjson::Document document;
		document.Parse(serializedHeaders.c_str());

		HttpHeaders headers;
		for (rapidjson::Document::ConstMemberIterator it = document.MemberBegin(); it != document.MemberEnd(); ++it) {
			headers.insert({ it->name.GetString(), it->value.GetString() });
		}

		return headers;
	}

	std::optional<ResponseCacheControl> convertStringToResponseCacheControl(const std::string& serializedResponseCacheControl) {
		if (serializedResponseCacheControl.empty()) {
			return std::nullopt;
		}

		rapidjson::Document document;
		document.Parse(serializedResponseCacheControl.c_str());
		return ResponseCacheControl{
			document["mustRevalidate"].GetBool(),
			document["noCache"].GetBool(),
			document["noStore"].GetBool(),
			document["noTransform"].GetBool(),
			document["accessControlPublic"].GetBool(),
			document["accessControlPrivate"].GetBool(),
			document["proxyRevalidate"].GetBool(),
			document["maxAge"].GetInt(),
			document["sharedMaxAge"].GetInt()
		};
	}
}