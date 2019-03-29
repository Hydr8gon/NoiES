/*
    Copyright 2019 Hydr8gon

    This file is part of NoiES.

    NoiES is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NoiES is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NoiES. If not, see <https://www.gnu.org/licenses/>.
*/

#include <cstdio>
#include <string>
#include <vector>

#include "core.h"

namespace config
{

int disableSpriteLimit = 0;
int screenFiltering    = 0;
int frameLimiter       = 1;
int keyMap[8];

typedef struct
{
    string name;
    int *value;
} Setting;

const vector<Setting> settings =
{
    { "disableSpriteLimit", &disableSpriteLimit },
    { "screenFiltering",    &screenFiltering    },
    { "frameLimiter",       &frameLimiter       },
    { "keyA",               &keyMap[0]          },
    { "keyB",               &keyMap[1]          },
    { "keySelect",          &keyMap[2]          },
    { "keyStart",           &keyMap[3]          },
    { "keyUp",              &keyMap[4]          },
    { "keyDown",            &keyMap[5]          },
    { "keyLeft",            &keyMap[6]          },
    { "keyRight",           &keyMap[7]          }
};

void load()
{
    FILE *config = fopen("noies.ini", "r");
    if (!config)
        return;

    // Search for setting names in the config file and load their values when found
    char read[256];
    while (fgets(read, 256, config) != NULL)
    {
        string line = read;
        for (unsigned int i = 0; i < settings.size(); i++)
        {
            if (line.substr(0, line.rfind("=")) == settings[i].name)
            {
                try
                {
                    *settings[i].value = stoi(line.substr(line.rfind("=") + 1));
                }
                catch (...)
                {
                    // Keep the default value if the config value is invalid
                }
            }
        }
    }

    fclose(config);
}

void save()
{
    // Save all setting names and values to the config file
    FILE *config = fopen("noies.ini", "w");
    for (unsigned int i = 0; i < settings.size(); i++)
        fputs((settings[i].name + "=" + to_string(*settings[i].value) +"\n").c_str(), config);
    fclose(config);
}

}
