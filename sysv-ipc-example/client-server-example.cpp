#include <algorithm>
#include <iostream>
#include <numeric>

#include <unistd.h>

#include "ipc.hpp"

#ifndef NDEBUG
  #define DBG(STMT) STMT;
  #define CLIENT(STMT) cout << "[Client " << pid << "] " #STMT << endl; STMT;
  #define SERVER(STMT) cout << "[Server] " #STMT << endl; STMT;
#else
  #define DBG(STMT)
  #define CLIENT(STMT) STMT;
  #define SERVER(STMT) STMT;
#endif // NDEBUG

constexpr unsigned
  NO_OF_CLIENTS             = 5;
  
constexpr unsigned
  NO_OF_PACKETS_PER_CLIENT  = 20;
  
constexpr unsigned
  NO_OF_ITEMS_IN_PACKET     = 10;
  
constexpr unsigned
  NO_OF_SERVER_TRANSACTIONS = NO_OF_CLIENTS * NO_OF_PACKETS_PER_CLIENT;

constexpr char const *
  SHARED_KEY                = __FILE__;

using std::clog;
using std::cout;
using std::endl;

enum : unsigned short
{
  SemClient = 0,
  SemServer,
  SemResultReady,
  SemConsole,
  
  SemNum // total amount of semaphores
};

struct Packet
{
  int numbers [NO_OF_ITEMS_IN_PACKET];
  int result;
};

void
Client ()
{
  const ::pid_t pid = ::getpid ();
  DBG (clog << "Starting client " << pid << endl)
  
  int sum = 0;
  Packet *const pData = static_cast<Packet*> (GetShm ());
  
  for (unsigned i = 0; i < NO_OF_PACKETS_PER_CLIENT; ++i) {
    // wait for server access
    CLIENT (P (SemServer))
    // generate data:
    // easy to predict the result but still individual for every client
    std::fill_n (pData->numbers, NO_OF_ITEMS_IN_PACKET, pid);
    // notify the server
    CLIENT (V (SemClient))
    
    // wait for the server and get the result
    CLIENT (P (SemResultReady))
    sum += pData->result;
    
    // free server access
    CLIENT (V (SemServer))
  }
  
  const int expected = NO_OF_PACKETS_PER_CLIENT * NO_OF_ITEMS_IN_PACKET * pid;
  
  CLIENT (P (SemConsole))
  cout << "Client " << pid << '\n'
    << "\tresult   : " << sum << '\n'
    << "\texpected : " << expected
    << " [" << ( sum == expected ? "OK" : "failed" ) << ']'
    << endl;
  CLIENT (V (SemConsole))
  
  DBG (clog << "Terminating client " << pid << endl)
}

void
Server () 
{
  DBG (clog << "Starting server " << ::getpid() << "..." << endl)
  
  Packet *const pData = static_cast<Packet*> (GetShm ());
  
  for (unsigned i = 0; i < NO_OF_SERVER_TRANSACTIONS; ++i) {
    // wait for a client
    SERVER (P (SemClient))
    // process the data
    pData->result = std::accumulate (pData->numbers,
      pData->numbers + NO_OF_ITEMS_IN_PACKET, 0);
    // notify the client
    SERVER (V (SemResultReady))
  }
  
  DBG (clog << "Terminating server..." << endl)
}

int
main ()
{
  DBG (clog << "Start main " << ::getpid () << "..." << endl)
  
  // initialize all the *X-style stuff for IPC
  InitIpc (SHARED_KEY, sizeof (Packet), SemNum);
  
  // initialize semaphores to true
  V (SemServer);
  V (SemConsole);
  
  // run server
  Fork (Server);
  // run clients
  for (unsigned i = 0; i < NO_OF_CLIENTS; ++i) {
    Fork (Client);
  }
  
  // wait for all the party
  JoinAll ();
  
  DBG (clog << "Terminating main..." << endl)
  return EXIT_SUCCESS;
}

