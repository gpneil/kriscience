#ifndef __IPC_HPP__
#define __IPC_HPP__

#include <cstddef>
#include <functional>

void
InitIpc (const char *key, const size_t memSize, const int semNum);

void *const
GetShm ();

void
Fork (const std::function<void ()> proc);

void
JoinAll ();

void
P (const unsigned short sem);

void
V (const unsigned short sem);

#endif // __IPC_HPP__

