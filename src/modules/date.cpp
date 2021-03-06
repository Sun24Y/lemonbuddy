#include "bar.hpp"
#include "lemonbuddy.hpp"
#include "modules/date.hpp"
#include "utils/config.hpp"
#include "utils/string.hpp"

using namespace modules;

DateModule::DateModule(std::string name_)
  : TimerModule(name_, 1s), builder(std::make_unique<Builder>())
{
  this->interval = std::chrono::duration<double>(
    config::get<float>(name(), "interval", 1));

  this->formatter->add(DEFAULT_FORMAT, TAG_DATE, { TAG_DATE });

  this->date = config::get<std::string>(name(), "date");
  this->date_detailed = config::get<std::string>(name(), "date_detailed", "");
}

bool DateModule::update()
{
  if (!this->formatter->has(TAG_DATE))
    return false;

  auto date_format = this->detailed ? this->date_detailed : this->date;
  auto time = std::time(nullptr);
  char new_str[256] = {0,};
  std::strftime(new_str, sizeof(new_str), date_format.c_str(), std::localtime(&time));

  if (std::strncmp(new_str, this->date_str, sizeof(new_str)) == 0)
    return false;
  else
    std::memmove(this->date_str, new_str, sizeof(new_str));

  return true;
}

std::string DateModule::get_output()
{
  if (!this->date_detailed.empty())
    this->builder->cmd(Cmd::LEFT_CLICK, EVENT_TOGGLE);
  this->builder->node(this->TimerModule::get_output());
  return this->builder->flush();
}

bool DateModule::build(Builder *builder, std::string tag)
{
  if (tag == TAG_DATE)
    builder->node(this->date_str);
  return tag == TAG_DATE;
}

bool DateModule::handle_command(std::string cmd)
{
  if (cmd == EVENT_TOGGLE) {
    this->detailed = !this->detailed;
    this->wakeup();
  }
  return cmd == EVENT_TOGGLE;
}
