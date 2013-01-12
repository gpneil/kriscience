#include <algorithm> // std::min
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dlfcn.h>
#include <pthread.h>
#include <sys/types.h>

// I prefer an immediate SIGSEGV abortion in preference to abort() function call
// as I will have the current function context/state handy
// (stack, variables etc.) instead of ending up somewhere in abort()/raise()...
// whoever knows where else
#define ABORT_HERE *((int*)0) = 0

template<class Fn>
Fn getNextFunction(char const* const name) {
  ::dlerror();
  if (void* const sym = ::dlsym(RTLD_NEXT, name)) {
    return reinterpret_cast<Fn>(sym);
  }
  
  ABORT_HERE;
}

// information stored at the beggining of the allocated block
struct MallocInfo {
  size_t magic1[4];
  
  size_t raNew;            // malloc() caller address
  pthread_t tidCreator;    // malloc() caller Thread ID
  size_t raFree;           // free() caller address
  pthread_t tidTerminator; // free() caller Thread ID
  size_t size;             // allocated memory size
  size_t freeCnt;          // free() calls counter
  
  size_t magic2[4];
};

// information stored at the end of the block
struct MallocInfoBack {
  size_t magic1[4];
  
  pthread_t tidCreator; // malloc() caller Thread ID
  size_t size;          // allocated memory size
  
  size_t magic2[4];
};

// it's good to have LSBs set so there's a great chance we'll get SIGBUS if
// anyone attempts to execute it
const size_t MAGIC1 = 0xaaaaaaaau;
const size_t MAGIC2 = 0xbbbbbbbbu;
const size_t MAGIC3 = 0x55555555u;
const size_t MAGIC4 = 0xddddddddu;

// helper variables (see the comment in free())
size_t gRaFree = 0;
pthread_t gTidTerminator = 0;

void* malloc(size_t size) {
  size_t ra;
  asm volatile("move %0, $ra" : "=r" (ra)); // get the return address
  
  typedef void*(*fn_malloc_t)(size_t);
  static fn_malloc_t fn_malloc = getNextFunction<fn_malloc_t>("malloc");
  
  char* const ptr = static_cast<char*>(fn_malloc(
    sizeof(MallocInfo) +
    size +
    sizeof(MallocInfoBack)));
  
  if (!ptr) {
    return ptr;
  }
  
  const MallocInfo info = {
    { MAGIC1, MAGIC1, MAGIC1, MAGIC1 },
    ra, pthread_self(), 0u, 0u, size, 0u,
    { MAGIC2, MAGIC2, MAGIC2, MAGIC2 }
    };
  
  const MallocInfoBack infoBack = {
    { MAGIC3, MAGIC3, MAGIC3, MAGIC3 },
    pthread_self(), size,
    { MAGIC4, MAGIC4, MAGIC4, MAGIC4 }
  };
  
  *reinterpret_cast<MallocInfo*>(ptr) = info;
  *reinterpret_cast<MallocInfoBack*>(ptr + sizeof(MallocInfo) + size) =
    infoBack;
  
  return ptr + sizeof(MallocInfo);
}

void free(void* ptr) {
  size_t ra;
  asm volatile("move %0, $ra" : "=r" (ra)); // get the return address
  typedef void (*fn_free_t)(void*);
  static fn_free_t fn_free = getNextFunction<fn_free_t>("free");
  
  if (ptr) {
    ptr = static_cast<char*>(ptr) - sizeof(MallocInfo);
    MallocInfo* const mi = reinterpret_cast<MallocInfo*>(ptr);
    
    if (0 != __sync_fetch_and_add(&mi->freeCnt, 1)) {
      // this is it - someone alreade freed the memory
      
      // now these two bits of information are preserved in the global variables
      // as the compiler may (re)use registers and stack heavily and it's easier
      // to find out in the disassembly where these values are stored when they
      // are assigned to global variables
      gRaFree = mi->raFree;
      gTidTerminator = mi->tidTerminator;
      
      ABORT_HERE;
    }
    
    mi->raFree = ra;
    mi->tidTerminator = pthread_self();
  }
  
  fn_free(ptr);
}

void* calloc(size_t nmemb, size_t size) {
  void* const ptr = malloc(nmemb * size);
  if (ptr) {
    std::memset(ptr, 0, nmemb * size);
  }
  
  return ptr;
}

void* realloc(void *ptr, size_t size) {
  if (!ptr) {
    return malloc(size);
  }
  else if (0 == size) {
    free(ptr);
    return NULL;
  }
  else {
    const size_t minSize =
      std::min(reinterpret_cast<MallocInfo*>(
        static_cast<char*>(ptr) - sizeof(MallocInfo))->size, size);
    
    void* const ptrNew = malloc(size);
    if (ptrNew) {
      memcpy(ptrNew, ptr, minSize);
      free(ptr);
    }
  
  return ptrNew;
  }
}

