#include "config.hpp"

#include <thread>
#include <vector>
#include <sstream>

#include <i3ipc++/ipc-util.hpp>

#include "lemonbuddy.hpp"
#include "bar.hpp"
#include "modules/i3.hpp"
#include "utils/config.hpp"
#include "utils/io.hpp"
#include "utils/macros.hpp"
#include "utils/string.hpp"

using namespace modules;
using namespace i3;

#define DEFAULT_WS_ICON "workspace_icon-default"
#define DEFAULT_WS_LABEL "%icon% %name%"

// TODO: Needs more testing
// TODO: Add mode indicators

i3Module::i3Module(std::string name_, std::string monitor)
  : EventModule(name_)
  , monitor(monitor)
{
  try {
    this->ipc = std::make_unique<i3ipc::connection>();
  } catch (std::runtime_error &e) {
    throw ModuleError(e.what());
  }

  this->local_workspaces
    = config::get<bool>(name(), "local_workspaces", this->local_workspaces);
  this->workspace_name_strip_nchars
    = config::get<std::size_t>(name(), "workspace_name_strip_nchars", this->workspace_name_strip_nchars);

  this->formatter->add(DEFAULT_FORMAT, TAG_LABEL_STATE, { TAG_LABEL_STATE });

  if (this->formatter->has(TAG_LABEL_STATE)) {
    this->state_labels.insert(std::make_pair(WORKSPACE_FOCUSED, drawtypes::get_optional_config_label(name(), "label-focused", DEFAULT_WS_LABEL)));
    this->state_labels.insert(std::make_pair(WORKSPACE_UNFOCUSED, drawtypes::get_optional_config_label(name(), "label-unfocused", DEFAULT_WS_LABEL)));
    this->state_labels.insert(std::make_pair(WORKSPACE_VISIBLE, drawtypes::get_optional_config_label(name(), "label-visible", DEFAULT_WS_LABEL)));
    this->state_labels.insert(std::make_pair(WORKSPACE_URGENT, drawtypes::get_optional_config_label(name(), "label-urgent", DEFAULT_WS_LABEL)));
    this->state_labels.insert(std::make_pair(WORKSPACE_DIMMED, drawtypes::get_optional_config_label(name(), "label-dimmed")));
  }

  this->icons = std::make_unique<drawtypes::IconMap>();
  this->icons->add(DEFAULT_WS_ICON, std::make_unique<drawtypes::Icon>(config::get<std::string>(name(), DEFAULT_WS_ICON, "")));

  for (auto workspace : config::get_list<std::string>(name(), "workspace_icon", {})) {
    auto vec = string::split(workspace, ';');
    if (vec.size() == 2) this->icons->add(vec[0], std::make_unique<drawtypes::Icon>(vec[1]));
  }
}

i3Module::~i3Module()
{
  if (!this->ipc)
    return;
  // FIXME: Hack to release the recv lock. Will need to patch the ipc lib
  this->ipc->send_command("workspace back_and_forth");
  this->ipc->send_command("workspace back_and_forth");
}

void i3Module::start()
{
  // this->ipc->subscribe(i3ipc::ET_WORKSPACE | i3ipc::ET_OUTPUT | i3ipc::ET_WINDOW);
  this->ipc->subscribe(i3ipc::ET_WORKSPACE | i3ipc::ET_OUTPUT);
  this->ipc->prepare_to_event_handling();
  this->EventModule<i3Module>::start();
}

bool i3Module::has_event()
{
  if (!this->ipc || !this->enabled())
    return false;
  this->ipc->handle_event();
  return true;
}

bool i3Module::update()
{
  if (!this->enabled())
    return false;

  i3ipc::connection connection;

  try {
    // for (auto &&m : connection.get_outputs()) {
    //   if (m->name == this->monitor) {
    //     monitor_focused = m->active;
    //     break;
    //   }
    // }

    this->workspaces.clear();

    auto workspaces = connection.get_workspaces();

    std::string focused_monitor;

    for (auto &&ws : workspaces) {
      if (ws->focused) {
        focused_monitor = ws->output;
        break;
      }
    }

    bool monitor_focused = (focused_monitor == this->monitor);

    for (auto &&ws : connection.get_workspaces()) {
      if (this->local_workspaces && ws->output != this->monitor)
        continue;

      Flag flag = WORKSPACE_NONE;

      if (ws->focused)
        flag = WORKSPACE_FOCUSED;
      else if (ws->urgent)
        flag = WORKSPACE_URGENT;
      else if (ws->visible)
        flag = WORKSPACE_VISIBLE;
      else
        flag = WORKSPACE_UNFOCUSED;

      // if (!monitor_focused)
      //   flag = WORKSPACE_DIMMED;

      auto workspace_name = ToStr(ws->name);
      if (this->workspace_name_strip_nchars > 0 && workspace_name.length() > this->workspace_name_strip_nchars)
        workspace_name.erase(0, this->workspace_name_strip_nchars);

      std::unique_ptr<drawtypes::Icon> &icon = this->icons->get(workspace_name, DEFAULT_WS_ICON);
      std::unique_ptr<drawtypes::Label> label = this->state_labels.find(flag)->second->clone();

      if (!monitor_focused)
        label->replace_defined_values(this->state_labels.find(WORKSPACE_DIMMED)->second);

      label->replace_token("%name%", workspace_name);
      label->replace_token("%icon%", icon->text);
      label->replace_token("%index%", std::to_string(ws->num));

      this->workspaces.emplace_back(std::make_unique<Workspace>(ws->num, flag, std::move(label)));
    }
  } catch (std::runtime_error &e) {
    this->logger->error(e.what());
  }

  return true;
}

bool i3Module::build(Builder *builder, std::string tag)
{
  if (tag != TAG_LABEL_STATE)
    return false;

  for (auto &&ws : this->workspaces) {
    builder->cmd(Cmd::LEFT_CLICK, std::string(EVENT_CLICK) + std::to_string(ws.get()->idx));
      builder->node(ws.get()->label);
    builder->cmd_close(true);
  }

  return true;
}

bool i3Module::handle_command(std::string cmd)
{
  if (cmd.find(EVENT_CLICK) == std::string::npos || cmd.length() < std::strlen(EVENT_CLICK))
    return false;

  this->ipc->send_command("workspace number "+ cmd.substr(std::strlen(EVENT_CLICK)));

  return true;
}
