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

#include <switch.h>

void *createMutex()
{
    Mutex *mutex = new Mutex;
    mutexInit(mutex);
    return mutex;
}

void lockMutex(void *mutex)
{
    mutexLock((Mutex*)mutex);
}

void unlockMutex(void *mutex)
{
    mutexUnlock((Mutex*)mutex);
}
