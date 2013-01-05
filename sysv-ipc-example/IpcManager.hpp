#ifndef __IPCMANAGER_HPP__
#define __IPCMANAGER_HPP__

#include <cassert>
#include <cstdlib>
#include <list>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

class IpcManager
{
public:
  IpcManager (const char *key, const size_t memSize, const int semNum);
  
  ~IpcManager ();

  void *const
  GetShm ();
  
  void
  Fork (const std::function<void ()> proc);

  void
  JoinAll () const;

  void
  P (const unsigned short sem) const;

  void
  V (const unsigned short sem) const;
  
private:
  typedef std::list< ::pid_t> Pids;

  int m_key;
  int m_memId;
  int m_semId;
  bool m_bCreator;
  void *m_data;
  Pids m_pids;
};

#endif // __IPCMANAGER_HPP__
