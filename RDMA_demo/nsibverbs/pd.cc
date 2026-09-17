#include "verbs.h"
#include "command.h"
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <stdint.h>


using namespace std;
using namespace boost::interprocess;
extern "C" {
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context) {
  struct ibv_pd * pd = (struct ibv_pd *)malloc(sizeof(struct ibv_pd));
  pd->context = context;
  pd->handle = 0;
  return pd;
}

int ibv_dealloc_pd(struct ibv_pd *pd) {
  free(pd);
  return 0;
}

}

