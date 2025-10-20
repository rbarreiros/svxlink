/**
 * @file   AsyncConfigSource.h
 * @brief  Configuration source URL parser and backend detection
 * @author Tobias Blomberg / SM0SVX & Ricardo Barreiros / CT7ALW
 * @date   2025-01-XX
 *
 * URL parsing and backend detection for configuration backends.
 * Separates parsing logic from factory pattern.
 *
 * Supports:
 *   - file://path/to/file.conf
 *   - sqlite://path/to/db.sqlite
 *   - mysql://user:pass@host:port/database
 *   - postgresql://user:pass@host:port/database
 */

#ifndef ASYNC_CONFIG_SOURCE_INCLUDED
#define ASYNC_CONFIG_SOURCE_INCLUDED

#include <string>
#include <vector>
#include <optional>

namespace Async
{

/**
 * @brief Configuration source parser and backend detector
 *
 * Parses configuration source URLs and provides information about
 * backend types and availability. Does not create backends itself.
 *
 * Example:
 * @code
 * auto source = ConfigSource::parse("mysql://user:pass@localhost/svxlink");
 * if (source && source->isValid()) {
 *   std::cout << "Backend: " << source->backend_type_name << std::endl;
 *   std::cout << "Connection: " << source->connection_info << std::endl;
 * }
 * @endcode
 */
class ConfigSource
{
public:
  /**
   * @brief Enumeration of supported backend types
   */
  enum BackendType
  {
    BACKEND_FILE,        ///< File-based configuration
    BACKEND_SQLITE,      ///< SQLite database
    BACKEND_MYSQL,       ///< MySQL database
    BACKEND_POSTGRESQL,  ///< PostgreSQL database
    BACKEND_UNKNOWN      ///< Unknown or invalid backend
  };

  /**
   * @brief Backend type name (e.g., "file", "sqlite")
   */
  std::string backend_type_name;

  /**
   * @brief Connection information (file path or connection string)
   */
  std::string connection_info;

  /**
   * @brief Detected backend type
   */
  BackendType backend_type;

  /**
   * @brief Parse a configuration source URL
   * @param url The URL to parse (e.g., "file://path" or "mysql://...")
   * @return Optional ConfigSource if parsing succeeds, nullopt otherwise
   */
  static std::optional<ConfigSource> parse(const std::string& url);

  /**
   * @brief Check if a backend type is available (compiled in)
   * @param backend_type_name Backend name (e.g., "sqlite", "mysql")
   * @return true if the backend is available
   */
  static bool isBackendAvailable(const std::string& backend_type_name);

  /**
   * @brief Check if a backend type is available (compiled in)
   * @param type Backend type enumeration
   * @return true if the backend is available
   */
  static bool isBackendAvailable(BackendType type);

  /**
   * @brief Get list of available backend names
   * @return Vector of backend names (e.g., ["file", "sqlite", "mysql"])
   */
  static std::vector<std::string> availableBackends();

  /**
   * @brief Get comma-separated string of available backends
   * @return String like "file, sqlite, mysql"
   */
  static std::string availableBackendsString();

  /**
   * @brief Check if this source is valid
   * @return true if backend_type is not BACKEND_UNKNOWN
   */
  bool isValid() const { return backend_type != BACKEND_UNKNOWN; }

private:
  /**
   * @brief Detect backend type from URL
   * @param url The URL to analyze
   * @return Detected backend type
   */
  static BackendType detectBackendType(const std::string& url);

  /**
   * @brief Get backend type name from enum
   * @param type Backend type
   * @return Name string (e.g., "sqlite")
   */
  static std::string getBackendTypeName(BackendType type);

  /**
   * @brief Parse database URL into connection string
   * @param url The database URL
   * @param connection_string Output connection string
   * @return true if parsing succeeds
   */
  static bool parseDatabaseURL(const std::string& url,
                               std::string& connection_string);
};

} // namespace Async

#endif // ASYNC_CONFIG_SOURCE_INCLUDED

