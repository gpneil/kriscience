#include <cassert>

#include "ipc.hpp"
#include "IpcManager.hpp"

IpcManager *g_pIpc = nullptr;

void
InitIpc (const char *key, const size_t memSize, const int semNum)
{
  // do not allow to reinitialize (nothing happens but should not occur)
  assert (nullptr == g_pIpc);

  static IpcManager ipcMan (key, memSize, semNum);
  g_pIpc = &ipcMan;
}

void *const
GetShm ()
{
  assert (g_pIpc);
  return g_pIpc->GetShm ();
}

void Fork (const std::function<void ()> proc)
{
  assert (g_pIpc);
  g_pIpc->Fork (proc);
}

void
JoinAll ()
{
  assert (g_pIpc);
  g_pIpc->JoinAll ();
}

void
P (const unsigned short sem)
{
  assert (g_pIpc);
  g_pIpc->P (sem);
}

void
V (const unsigned short sem)
{
  assert (g_pIpc);
  g_pIpc->V (sem);
}

