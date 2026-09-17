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
struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
			  int access) {
  mlock(addr, length);
  struct ibv_mr *mr = (struct ibv_mr *)malloc(sizeof(struct ibv_mr));
  mr->context = pd->context;
  mr->pd = pd;
  mr->addr = addr;
  mr->length = length;
  mr->handle = 0;
  // 虽然在模拟器中 我们不关注lkey和rkey，但是为了和NCCL对接，这里还是返回一个固定的值
  // 因为在NCCL中 会有对于lkey和rkey非零的判断
  mr->lkey = 100;
  mr->rkey = 100;
  return mr;
}

int ibv_dereg_mr(struct ibv_mr *mr) {
  munlock(mr->addr, mr->length);
  free(mr);
  return 0;
}

}

