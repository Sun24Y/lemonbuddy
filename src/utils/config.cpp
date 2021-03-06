#include "services/logger.hpp"
#include "utils/config.hpp"
#include "utils/io.hpp"

namespace config
{
  boost::property_tree::ptree pt;
  std::string file_path;
  std::string bar_path;

  void set_bar_path(std::string path)
  {
    bar_path = path;
  }

  std::string get_bar_path()
  {
    return bar_path;
  }

  void load(std::string path)
  {
    if (!io::file::exists(path)) {
      throw UnexistingFileError("Could not find configuration file \""+ path + "\"");
    }

    log_trace(path);

    try {
      boost::property_tree::read_ini(path, pt);
    } catch (std::exception &e) {
      throw ParseError(e.what());
    }

    file_path = path;
  }

  void load(const char *dir, std::string path) {
    load(std::string(dir != nullptr ? dir : "") +"/"+ path);
  }

  // void reload()
  // {
  //   try {
  //     boost::property_tree::read_ini(file_path, pt);
  //   } catch (std::exception &e) {
  //     throw ParseError(e.what());
  //   }
  // }

  boost::property_tree::ptree get_tree() {
    return pt;
  }

  std::string build_path(std::string section, std::string key) {
    return section +"."+ key;
  }

  std::string get_file_path() {
    const char *env_home = std::getenv("HOME");
    if (env_home != nullptr) {
      return string::replace(file_path, env_home, "~");
    }
    return file_path;
  }
}
