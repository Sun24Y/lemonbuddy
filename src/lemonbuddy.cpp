#include <iostream>
#include <signal.h>
#include <sys/stat.h>
#include <thread>

#include "version.hpp"
#include "bar.hpp"
#include "config.hpp"
#include "eventloop.hpp"
#include "lemonbuddy.hpp"
#include "registry.hpp"
#include "modules/base.hpp"
#include "services/builder.hpp"

#include "utils/cli.hpp"
#include "utils/config.hpp"
#include "utils/io.hpp"
#include "utils/macros.hpp"
#include "utils/proc.hpp"
#include "utils/string.hpp"

/**
 * TODO: Reload config on USR1
 * TODO: Add more documentation
 * TODO: Simplify overall flow
 */

std::mutex pid_mtx;
std::vector<pid_t> pids;

void register_pid(pid_t pid) {
  std::lock_guard<std::mutex> lck(pid_mtx);
  pids.emplace_back(pid);
}
void unregister_pid(pid_t pid) {
  std::lock_guard<std::mutex> lck(pid_mtx);
  pids.erase(std::remove(pids.begin(), pids.end(), pid), pids.end());
}

/**
 * Main entry point woop!
 */
int main(int argc, char **argv)
{
  int retval = EXIT_SUCCESS;

  std::unique_ptr<EventLoop> eventloop;
  std::shared_ptr<Logger> logger = get_logger();

  try {
    auto usage = "Usage: "+ std::string(argv[0]) + " bar_name [OPTION...]";

    cli::add_option("-h", "--help", "Show help options");
    cli::add_option("-c", "--config", "FILE", "Path to the configuration file");
    cli::add_option("-p", "--pipe", "FILE", "Path to the input pipe");
    cli::add_option("-l", "--log", "LEVEL", "Set the logging verbosity", {"warning","info","debug","trace"});
    cli::add_option("-d", "--dump", "PARAM", "Show value of PARAM in section [bar_name]");
    cli::add_option("-x", "--print-exec", "Print the generated command line string used to start the lemonbar process");
    cli::add_option("-w", "--print-wmname", "Print the generated WM_NAME");
    cli::add_option("-v", "--version", "Print version information");

    /**
     * Parse command line arguments
     */
    if (argc < 2 || cli::is_option(argv[1], "-h", "--help"))
      cli::usage(usage, (argc > 1 && cli::is_option(argv[1], "-h", "--help")));

    cli::parse(2, argc, argv);

    if (cli::has_option("version") || cli::is_option(argv[1], "-v", "--version")) {
      std::cout << APP_NAME << " " << GIT_TAG << std::endl;

      if (std::strncmp(argv[1], "-vv", 3) == 0) {
        std::cout << "\n" << "Built with: "
          << (ENABLE_ALSA    ? "+" : "-") << "alsa "
          << (ENABLE_I3      ? "+" : "-") << "i3 "
          << (ENABLE_MPD     ? "+" : "-") << "mpd "
          << (ENABLE_NETWORK ? "+" : "-") << "network "
          << "\n\n";

        if (ENABLE_ALSA)
          std::cout
            << "ALSA_SOUNDCARD        " << ALSA_SOUNDCARD << std::endl;

        std::cout
            << "CONNECTION_TEST_IP    " << CONNECTION_TEST_IP << "\n"
            << "PATH_BACKLIGHT_VAL    " << PATH_BACKLIGHT_VAL << "\n"
            << "PATH_BACKLIGHT_MAX    " << PATH_BACKLIGHT_MAX << "\n"
            << "BSPWM_SOCKET_PATH     " << BSPWM_SOCKET_PATH << "\n"
            << "BSPWM_STATUS_PREFIX   " << BSPWM_STATUS_PREFIX << "\n"
            << "PATH_CPU_INFO         " << PATH_CPU_INFO << "\n"
            << "PATH_MEMORY_INFO      " << PATH_MEMORY_INFO << "\n";
      }
      return EXIT_SUCCESS;
    }

    if (cli::has_option("help") || argv[1][0] == '-')
      cli::usage(usage, false);

    /**
     * Set logging verbosity
     */
    if (cli::has_option("log")) {
      logger->set_level(LogLevel::LEVEL_ERROR);

      if (cli::match_option_value("log", "warning"))
        logger->set_level(LogLevel::LEVEL_WARNING);
      else if (cli::match_option_value("log", "info"))
        logger->add_level(LogLevel::LEVEL_WARNING | LogLevel::LEVEL_INFO);
      else if (cli::match_option_value("log", "debug"))
        logger->add_level(LogLevel::LEVEL_WARNING | LogLevel::LEVEL_INFO | LogLevel::LEVEL_DEBUG);
      else if (cli::match_option_value("log", "trace")) {
        logger->add_level(LogLevel::LEVEL_WARNING | LogLevel::LEVEL_INFO | LogLevel::LEVEL_DEBUG);
#ifdef DEBUG
        logger->add_level(LogLevel::LEVEL_TRACE);
#endif
      }
    }

    /**
     * Load configuration file
     */
    const char *env_home = std::getenv("HOME");
    const char *env_xdg_config_home = std::getenv("XDG_CONFIG_HOME");

    if (cli::has_option("config")) {
      auto config_file = cli::get_option_value("config");

      if (env_home != nullptr)
        config_file = string::replace(cli::get_option_value("config"), "~", std::string(env_home));

      config::load(config_file);
    } else if (env_xdg_config_home != nullptr)
      config::load(env_xdg_config_home, "lemonbuddy/config");
    else if (env_home != nullptr)
      config::load(env_home, ".config/lemonbuddy/config");
    else
      throw ApplicationError("Could not find config file. Specify the location using --config=PATH");

    /**
     * Check if the specified bar exist
     */
    std::vector<std::string> defined_bars;
    for (auto &section : config::get_tree()) {
      if (std::strncmp("bar/", section.first.c_str(), 4) == 0)
        defined_bars.emplace_back(section.first.substr(4));
    }

    if (defined_bars.empty())
      logger->fatal("There are no bars defined in the config");

    auto bar_name = std::string(argv[1]);
    auto config_path = "bar/"+ bar_name;
    config::set_bar_path(config_path);

    if (std::find(defined_bars.begin(), defined_bars.end(), bar_name) == defined_bars.end()) {
      logger->error("The bar \""+ bar_name +"\" is not defined");
      logger->info("Available bars in "+ config::get_file_path() +": "+ string::join(defined_bars, ", "));
      return EXIT_FAILURE;
    }

    if (config::get_tree().get_child_optional(config_path) == boost::none)
      logger->fatal("Bar \""+ bar_name +"\" does not exist");

    /**
     * Dump specified config value
     */
    if (cli::has_option("dump")) {
      std::cout << config::get<std::string>(config_path, cli::get_option_value("dump")) << std::endl;
      return EXIT_SUCCESS;
    }

    if (cli::has_option("print-exec")) {
      std::cout << get_bar()->get_exec_line() << std::endl;
      return EXIT_SUCCESS;
    }

    if (cli::has_option("print-wmname")) {
      std::cout << get_bar()->opts->wm_name << std::endl;
      return EXIT_SUCCESS;
    }

    /**
     * Set path to input pipe file
     */
    std::string pipe_file;

    if (cli::has_option("pipe")) {
      pipe_file = cli::get_option_value("pipe");
    } else {
      pipe_file = "/tmp/lemonbuddy.pipe."
        + get_bar()->opts->wm_name
        + "."
        + std::to_string(proc::get_process_id());
      auto fptr = std::make_unique<io::file::FilePtr>(pipe_file, "a+");
      if (!*fptr)
        throw ApplicationError(StrErrno());
    }

    /**
     * Create and start the main event loop
     */
    eventloop = std::make_unique<EventLoop>(pipe_file);

    eventloop->start();
    eventloop->wait();

  } catch (Exception &e) {
    logger->error(e.what());
    retval = EXIT_FAILURE;
  }

  if (eventloop)
    eventloop->stop();

  /**
   * Terminate forked sub processes
   */
  if (!pids.empty()) {
    logger->info("Terminating "+ IntToStr(pids.size()) +" spawned process"+ (pids.size() > 1 ? "es" : ""));

    for (auto &&pid : pids)
      proc::kill(pid, SIGKILL);
  }

  if (eventloop)
    eventloop->cleanup();

  while (proc::wait_for_completion_nohang() > 0)
    ;

  log_trace("Reached end of application");

  return retval;
}
