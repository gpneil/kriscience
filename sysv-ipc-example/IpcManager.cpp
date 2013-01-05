#include <cerrno>
#include <cstring>
#include <iostream>

#include <sys/wait.h>

#include "ipc.hpp"
#include "IpcManager.hpp"

#ifndef NDEBUG
  #define DBG(STMT) STMT;
#else
  #define DBG(STMT)
#endif // NDEBUG

#define CHECK(EXPR) check_status ((EXPR), __FILE__, __LINE__)

namespace {

void check_status (const bool ok, const char* file, const int line)
{
  if (!ok) {
    std::cerr << "Error:" << std::strerror (errno)
      << " (" << file << ':' << line << ")\n";
    std::abort ();
  }
}

}

// only the first instance of the manager (creator) is responsible
// for resources
IpcManager::IpcManager (const char *key, const size_t memSize, const int semNum)
  : m_bCreator (true), m_data (nullptr)
{
  assert (key && key[0]);
  m_key = ::ftok (key, key[0]);
  CHECK (-1 != m_key);
  
  m_memId = ::shmget (m_key, memSize, IPC_CREAT | 0600);
  CHECK (-1 != m_memId);
  
  m_semId = ::semget (m_key, semNum, IPC_CREAT | 0600);
  CHECK (-1 != m_semId);
  
  for (int i = 0; i < semNum; ++i) {
    const int status = ::semctl (m_semId, i, SETVAL, nullptr);
    CHECK (-1 != status);
  }
}

IpcManager::~IpcManager ()
{
  if (m_data) {
    const int status = ::shmdt (m_data);
    CHECK (0 == status);
  }
  
  // only the original instance is responsible for resources
  if (m_bCreator) {
    DBG (std::clog << "~IpcManager() : freeing resources in " << ::getpid ()
      << std::endl)
    
    {  
      const int status = ::semctl (m_semId, 0, IPC_RMID, nullptr);
      CHECK (-1 != status);
    }
    
    {
      const int status = ::shmctl (m_memId, IPC_RMID, nullptr);
      CHECK (0 == status);
    }
  }
}

void *const
IpcManager::GetShm ()
{
  if (nullptr == m_data) {
    m_data = ::shmat (m_memId, nullptr, SHM_RND);
    CHECK (reinterpret_cast<void*> (-1) != m_data);
  }
  
  return m_data;
}

void
IpcManager::Fork (const std::function<void ()> proc)
{
  const ::pid_t pid = ::fork ();
  CHECK (-1 != pid);
  
  if (0 == pid) {
    // copy of IpcManager in child process cannot free resources
    // shared memory need to be attached
    m_bCreator = false;
    m_data = nullptr;
    
    proc ();
    
    std::exit (EXIT_SUCCESS);
  } else {
    // store pid for JoinAll()
    m_pids.push_back (pid);
  }
}

void
IpcManager::JoinAll () const
{
  for (const ::pid_t pid : m_pids)
  {
    DBG (std::clog << "Waiting for " << pid << std::endl)
    const ::pid_t status = ::waitpid (pid, nullptr, 0);
    CHECK (-1 != status);
    DBG (std::clog << "Finished waiting for " << pid << std::endl)
  }
}

void
IpcManager::P (const unsigned short sem) const
{
  ::sembuf sb = { sem, -1, 0 };
  const int status = ::semop (m_semId, &sb, 1);
  CHECK (0 == status);
}

void
IpcManager::V (const unsigned short sem) const
{
  ::sembuf sb = { sem, 1, 0 };
  const int status = ::semop (m_semId, &sb, 1);
  CHECK (0 == status);
}

