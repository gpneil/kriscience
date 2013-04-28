#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

std::mutex g_mutex;

void signal_handler(const int fd) {
  struct ::signalfd_siginfo si;
  
  while (const int bytes = ::read(fd, &si, sizeof(si))) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (sizeof(si) != bytes) {
      if (-1 == bytes) {
        std::perror("read failed");
      } else {
        std::cerr << "inconsistent read\n";
      }
      
      std::abort();
    }
    
    switch (si.ssi_signo) {
    case SIGQUIT:
    case SIGTERM:
    case SIGINT:
      std::clog << "Quitting application..." << std::endl;
      return;
      
    default:
      std::clog << "Received signal " << si.ssi_signo << std::endl;
      break;
    }
  }
}

int main() {
  ::sigset_t mask;
  if (0 != ::sigfillset(&mask)) {
    std::perror("sigfillset() failed");
    return EXIT_FAILURE;
  }

  if (0 != ::pthread_sigmask(SIG_BLOCK, &mask, NULL)) {
    std::perror("sigprocmask() failed");
    return EXIT_FAILURE;
  }

  const int fd = ::signalfd(-1, &mask, 0);
  if (fd < 0) {
    std::perror("signalfd() failed");
    return EXIT_FAILURE;
  }
  
  std::thread t(signal_handler, fd);
  
  // do some other stuff
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::clog << "Some application stuff..." << std::endl;
  }
  
  t.join();
  
  if (0 != ::close(fd)) {
    std::perror("close() failed");
    return EXIT_FAILURE;  
  }

  return EXIT_SUCCESS;
}

