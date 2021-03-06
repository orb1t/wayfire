#include <output.hpp>
#include "../../shared/config.hpp"
#include <core.hpp>
#include <linux/input.h>
#include <linux/input-event-codes.h>

#define NUMBER_COMMANDS 10

#define TYPE_COMMAND 1
#define TYPE_BINDING 2

class wayfire_command : public wayfire_plugin_t
{
    std::vector<key_callback> v;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "command";
        using namespace std::placeholders;


        auto section = config->get_section("command");

        std::vector<std::string> commands;
        for (auto command : section->options)
        {
            if (command.first.length() > 8 && command.first.substr(0, 8) == "command_")
            {
                commands.push_back(command.first.substr(8, command.first.size() - 8));
            }
        }

        v.resize(commands.size());
        int i = 0;

        for (auto num : commands)
        {
            auto command = "command_" + num;
            auto binding = "binding_" + num;

            auto comvalue = section->get_string(command, "");
            auto key = section->get_key(binding, {0, 0});

            if (key.keyval == 0 || command == "")
                continue;

            v[i++] = [=] (weston_keyboard* kbd, uint32_t key) {
                core->run(comvalue.c_str());
            };

            output->add_key((weston_keyboard_modifier)key.mod, key.keyval, &v[i - 1]);
        }

        if (commands.empty())
        {
            v.resize(1);
            v[0] = [] (weston_keyboard *, uint32_t)
            {
                core->run("weston-terminal");
            };
            output->add_key(MODIFIER_ALT, KEY_ENTER, &v.back());
        }
    }
};

extern "C"
{
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_command();
    }
}
