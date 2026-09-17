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
const char *ibv_event_type_str(enum ibv_event_type event) {
  switch (event)
  {
    case IBV_EVENT_CQ_ERR:              return "IBV_EVENT_CQ_ERR";
    case IBV_EVENT_QP_FATAL:            return "IBV_EVENT_QP_FATAL";
    case IBV_EVENT_QP_REQ_ERR:          return "IBV_EVENT_QP_REQ_ERR";
    case IBV_EVENT_QP_ACCESS_ERR:       return "IBV_EVENT_QP_ACCESS_ERR";
    case IBV_EVENT_COMM_EST:            return "IBV_EVENT_COMM_EST";
    case IBV_EVENT_SQ_DRAINED:          return "IBV_EVENT_SQ_DRAINED";
    case IBV_EVENT_PATH_MIG:            return "IBV_EVENT_PATH_MIG";
    case IBV_EVENT_PATH_MIG_ERR:        return "IBV_EVENT_PATH_MIG_ERR";
    case IBV_EVENT_DEVICE_FATAL:        return "IBV_EVENT_DEVICE_FATAL";
    case IBV_EVENT_PORT_ACTIVE:         return "IBV_EVENT_PORT_ACTIVE";
    case IBV_EVENT_PORT_ERR:            return "IBV_EVENT_PORT_ERR";
    case IBV_EVENT_LID_CHANGE:          return "IBV_EVENT_LID_CHANGE";
    case IBV_EVENT_PKEY_CHANGE:         return "IBV_EVENT_PKEY_CHANGE";
    case IBV_EVENT_SM_CHANGE:           return "IBV_EVENT_SM_CHANGE";
    case IBV_EVENT_SRQ_ERR:             return "IBV_EVENT_SRQ_ERR";
    case IBV_EVENT_SRQ_LIMIT_REACHED:   return "IBV_EVENT_SRQ_LIMIT_REACHED";
    case IBV_EVENT_QP_LAST_WQE_REACHED: return "IBV_EVENT_QP_LAST_WQE_REACHED";
    case IBV_EVENT_CLIENT_REREGISTER:   return "IBV_EVENT_CLIENT_REREGISTER";
    case IBV_EVENT_GID_CHANGE:          return "IBV_EVENT_GID_CHANGE";
    case IBV_EVENT_WQ_FATAL:            return "IBV_EVENT_WQ_FATAL";
    default:                            return "Unknown";
  }
}

int ibv_get_async_event(struct ibv_context *context,
			struct ibv_async_event *event) {
  while (true) {
    sleep(1);
  }
}
void ibv_ack_async_event(struct ibv_async_event *event) {
  return;
}
}

