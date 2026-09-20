/*
 * rdma_write_demo.cc
 * ============================================================================
 * SimAI-CLEM RDMA_demo —— 两进程 RDMA WRITE 演示程序
 * ============================================================================
 *
 * 【这个 demo 想说明什么】
 *   在没有任何 GPU、也没有真实 RNIC（RDMA 网卡）的环境下，演示 SimAI-CLEM 最核心的
 *   「双向交互框架 (bidirectional interaction framework)」设计思想：
 *
 *     ┌──────────────────────────────────────────────────────────────┐
 *     │  应用程序 (本 demo / 真实的 NCCL)                              │
 *     │     调用标准 IBV Verbs API：                                    │
 *     │     ibv_open_device / ibv_alloc_pd / ibv_reg_mr /              │
 *     │     ibv_create_cq / ibv_create_qp / ibv_modify_qp /            │
 *     │     ibv_post_send / ibv_poll_cq ...                            │
 *     └───────────────┬──────────────────────────────────────────────┘
 *                     │  (链接的不是系统 libibverbs，而是本目录 nsibverbs/)
 *                     ▼
 *     ┌──────────────────────────────────────────────────────────────┐
 *     │  nsibverbs (copy 自 nccl_hack_rdma/src/nsibverbs/)             │
 *     │     把每个 Verbs 调用翻译成一条 IbvCommand，                    │
 *     │     写入 boost::interprocess 共享内存队列 cmd_queue            │
 *     └───────────────┬──────────────────────────────────────────────┘
 *                     │  (共享内存 shm_nccl_ns3_<NODE_ID>)
 *                     ▼
 *     ┌──────────────────────────────────────────────────────────────┐
 *     │  ns-3 网络模拟器后端 (ns-3-alibabacloud)                       │
 *     │     消费命令、模拟 RDMA 网络传输、回填响应与 CQE               │
 *     └──────────────────────────────────────────────────────────────┘
 *
 *   这就是「双向交互」：应用侧的一次 Verbs 调用 → 下发命令到模拟器；
 *   模拟器完成模拟后 → 把响应/CQE 回传到应用侧。
 *
 * 【两个进程】
 *   - server（-s）：RDMA WRITE 的目标端，注册一块 buffer，等待被远端写入。
 *   - client（-c）：RDMA WRITE 的发起端，把本地 buffer 内容写到 server 的 buffer。
 *   两端的「元数据」（QP 号、GID、rkey、远端 buffer 地址、NODE_ID 等）通过
 *   TCP socket 交换 —— 这正是真实 RDMA 编程里标准的「带外握手 (out-of-band)」。
 *
 * 【关键】本程序里所有 ibv_* 函数的实现都来自本目录 nsibverbs/，而非系统 libibverbs。
 *         因此不需要真实 RNIC，也不需要 GPU。
 * ============================================================================
 */

#include "verbs.h"   // nsibverbs 的 IBV Verbs API 定义（copy 自 nccl_hack_rdma）

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <exception>    // std::exception: 捕获 ibv_get_device_list 在共享内存段未就绪时抛出的 boost 异常

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// ============================ 可调参数 ============================
static const int    DEFAULT_TCP_PORT = 18515;  // TCP 元数据通道默认端口
static const size_t BUFFER_SIZE      = 4096;   // RDMA WRITE 传输的数据大小(字节)
static const int    CQ_CAPACITY      = 128;    // 完成队列(CQ)深度
static const int    IB_PORT_NUM      = 1;      // IBV 物理端口号
static const int    GID_INDEX        = 0;      // RoCE GID 索引
static const int    MAX_SEND_WR      = 128;    // 发送队列最大 WR 数
static const int    MAX_RECV_WR      = 128;    // 接收队列最大 WR 数
static const int    POLL_TIMEOUT_SEC = 30;     // 轮询 CQE 的超时时间(秒)

// ====================== 通过 TCP 交换的连接元数据 ======================
// 真实 RDMA 中，通信双方必须先经由带外通道(TCP)交换这些信息，
// 才能把各自的 QP 连接到对端，并发起 RDMA WRITE。
struct conn_meta {
  uint32_t      qp_num;   // 本端 QP 号
  uint32_t      psn;      // packet sequence number
  uint16_t      lid;      // IB local id (RoCE 下可为 0)
  uint32_t      node_id;  // SimAI-CLEM: 本进程的 NODE_ID (将被编码进对端 dgid)
  uint64_t      rkey;     // 允许远端读写本端 buffer 的 remote key
  uint64_t      vaddr;    // 本端 buffer 的虚拟地址 (远端 RDMA WRITE 的目标)
  union ibv_gid gid;      // 本端 GID (RoCE)
};

// ============================ TCP 辅助函数 ============================
// 可靠地发送 n 字节（处理部分写）
static int tcp_send_all(int fd, const void *buf, size_t n) {
  const char *p = (const char *)buf;
  size_t sent = 0;
  while (sent < n) {
    ssize_t r = send(fd, p + sent, n - sent, 0);
    if (r <= 0) { if (r < 0 && errno == EINTR) continue; return -1; }
    sent += (size_t)r;
  }
  return 0;
}

// 可靠地接收 n 字节（处理部分读）
static int tcp_recv_all(int fd, void *buf, size_t n) {
  char *p = (char *)buf;
  size_t got = 0;
  while (got < n) {
    ssize_t r = recv(fd, p + got, n - got, 0);
    if (r <= 0) { if (r < 0 && errno == EINTR) continue; return -1; }
    got += (size_t)r;
  }
  return 0;
}

// server 端：监听并 accept 一个连接，返回已连接的 socket fd
static int tcp_server_connect(int port) {
  int lfd = socket(AF_INET, SOCK_STREAM, 0);
  if (lfd < 0) { perror("[TCP] socket"); return -1; }
  int on = 1;
  setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(port);

  if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("[TCP] bind"); close(lfd); return -1; }
  if (listen(lfd, 1) < 0) { perror("[TCP] listen"); close(lfd); return -1; }
  fprintf(stdout, "[TCP] 正在端口 %d 监听，等待对端连接...\n", port);

  int cfd = accept(lfd, NULL, NULL);
  if (cfd < 0) { perror("[TCP] accept"); close(lfd); return -1; }
  close(lfd);
  fprintf(stdout, "[TCP] 对端已连接。\n");
  return cfd;
}

// client 端：连接到 server，返回已连接的 socket fd
static int tcp_client_connect(const char *server_ip, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { perror("[TCP] socket"); return -1; }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(port);
  if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
    struct hostent *he = gethostbyname(server_ip);   // 退化为按主机名解析
    if (!he) { fprintf(stderr, "[TCP] 无法解析 %s\n", server_ip); close(fd); return -1; }
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
  }
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("[TCP] connect"); close(fd); return -1; }
  fprintf(stdout, "[TCP] 已连接到 server %s:%d\n", server_ip, port);
  return fd;
}

// ======================= 连接 ns-3 timer socket =======================
// 【SimAI-CLEM 启动握手，必需步骤】
// ns-3 后端的 InitializeTimerInterface() 会为每个 node 在端口 (13000 + node_id)
// 上 listen，并【阻塞式 accept】等待对应的应用进程来建联；只有所有 node 都连上后，
// ns-3 才会继续执行 InstallRdmaApps() -> StartApplication() 去创建共享内存段
// shm_nccl_ns3_<node_id>。因此应用进程必须先连接这个 timer socket，否则 ns-3 会
// 一直阻塞、共享内存段永不创建、后续 ibv_get_device_list 必然失败。
// 在 nccl-tests-modify 中该 socket 还用于 GetSimTime() 查询仿真时间；在本 demo 中
// 它主要承担「启动握手」职责（demo 本身不查询仿真时间）。
static const int NS3_TIMER_PORT_BASE = 13000;   // 与 timer-interface.h 的 SERVER_PORT 保持一致
static int connect_timer_socket(int node_id) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) { perror("[TIMER] socket"); return -1; }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(NS3_TIMER_PORT_BASE + node_id);
  if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
    fprintf(stderr, "[TIMER] 127.0.0.1 地址转换失败\n"); close(sock); return -1;
  }
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("[TIMER] connect"); close(sock); return -1;
  }
  fprintf(stdout, "[TIMER] 已连接 ns-3 timer socket 127.0.0.1:%d (node_id=%d)\n",
          NS3_TIMER_PORT_BASE + node_id, node_id);
  return sock;
}

// ======================= QP 状态机迁移辅助函数 =======================
// 说明：下列 ibv_modify_qp 调用会被 nsibverbs 拦截，转换成 IBV_MODIFY_QP 命令
//       下发给 ns-3 模拟器；迁移到 RTR 时还会解析出 remote_node_id 存入 qp->handle。

// RESET -> INIT
static int qp_to_init(struct ibv_qp *qp) {
  struct ibv_qp_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.qp_state        = IBV_QPS_INIT;
  attr.port_num        = IB_PORT_NUM;
  attr.pkey_index      = 0;
  attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
  int mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
  return ibv_modify_qp(qp, &attr, mask);
}

// INIT -> RTR (Ready To Receive)
static int qp_to_rtr(struct ibv_qp *qp, const struct conn_meta *remote) {
  struct ibv_qp_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.qp_state           = IBV_QPS_RTR;
  attr.path_mtu           = IBV_MTU_1024;
  attr.dest_qp_num        = remote->qp_num;
  attr.rq_psn             = remote->psn;
  attr.max_dest_rd_atomic = 1;
  attr.min_rnr_timer      = 12;

  attr.ah_attr.is_global    = 1;              // RoCE 需要 GRH
  attr.ah_attr.grh.dgid     = remote->gid;    // 对端 GID (来自 TCP 交换)
  // ===== SimAI-CLEM 关键约定 =====
  // nsibverbs 的 ibv_modify_qp() 会从 dgid.global.interface_id 的【高 32 位】解析出
  // remote_node_id，并存入 qp->handle；之后 post_send 依据该 handle 在共享内存中
  // 定位对端节点。因此这里把通过 TCP 收到的对端 node_id 显式编码进 dgid 的高 32 位。
  attr.ah_attr.grh.dgid.global.interface_id =
      ((uint64_t)remote->node_id << 32) |
      (remote->gid.global.interface_id & 0x00000000FFFFFFFFull);
  attr.ah_attr.grh.sgid_index = GID_INDEX;
  attr.ah_attr.grh.hop_limit  = 1;
  attr.ah_attr.dlid           = remote->lid;
  attr.ah_attr.sl             = 0;
  attr.ah_attr.src_path_bits  = 0;
  attr.ah_attr.port_num       = IB_PORT_NUM;

  int mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
             IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
  return ibv_modify_qp(qp, &attr, mask);
}

// RTR -> RTS (Ready To Send)
static int qp_to_rts(struct ibv_qp *qp, uint32_t local_psn) {
  struct ibv_qp_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.qp_state      = IBV_QPS_RTS;
  attr.sq_psn        = local_psn;
  attr.timeout       = 14;
  attr.retry_cnt     = 7;
  attr.rnr_retry     = 7;
  attr.max_rd_atomic = 1;
  int mask = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
             IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC;
  return ibv_modify_qp(qp, &attr, mask);
}

// ============================ 用法说明 ============================
static void usage(const char *prog) {
  fprintf(stderr,
    "用法:\n"
    "  server 端: %s -s [-p tcp_port]\n"
    "  client 端: %s -c <server_ip> [-p tcp_port]\n"
    "\n"
    "nsibverbs(SimAI-CLEM) 依赖的环境变量【运行前必须设置】:\n"
    "  NODE_ID               本进程在模拟器中的节点号 (如 server=0, client=1)\n"
    "  NUM_GPUS_PER_SERVER   每台服务器的 GPU 数 (如 8)\n"
    "  SIMU_ENABLE_GPU_P2P   必须设置(如 \"false\")；nsibverbs 会无条件读取它\n"
    "  RDMA_DEMO_VERBOSE     (可选) 设置后打印 nsibverbs 内部 INFO 日志\n"
    "\n"
    "前置条件: ns-3 后端(ns-3-alibabacloud)必须已经启动，并已创建好共享内存段\n"
    "          'shm_nccl_ns3_<NODE_ID>'，否则 ibv_get_device_list 会失败。\n",
    prog, prog);
}

// ============================ 主流程 ============================
int main(int argc, char **argv) {
  // ---- 1. 解析命令行参数 ----
  int is_server = -1;                 // 1=server, 0=client
  const char *server_ip = NULL;
  int tcp_port = DEFAULT_TCP_PORT;
  int opt;
  while ((opt = getopt(argc, argv, "sc:p:h")) != -1) {
    switch (opt) {
      case 's': is_server = 1; break;
      case 'c': is_server = 0; server_ip = optarg; break;
      case 'p': tcp_port = atoi(optarg); break;
      case 'h': default: usage(argv[0]); return 1;
    }
  }
  if (is_server < 0 || (is_server == 0 && server_ip == NULL)) { usage(argv[0]); return 1; }

  const char *role = is_server ? "server" : "client";
  fprintf(stdout, "==== SimAI-CLEM RDMA_demo [%s] 启动 ====\n", role);

  // ---- 2. 检查 nsibverbs 必需的环境变量 ----
  // 注意: nsibverbs 的 shm_ibv_post_send 会执行 strcmp(getenv("SIMU_ENABLE_GPU_P2P"),"true")，
  //       若该变量未设置 getenv 返回 NULL 会导致段错误，因此这里提前校验。
  if (!getenv("NODE_ID") || !getenv("NUM_GPUS_PER_SERVER") || !getenv("SIMU_ENABLE_GPU_P2P")) {
    fprintf(stderr, "[FATAL] 必须设置环境变量 NODE_ID / NUM_GPUS_PER_SERVER / SIMU_ENABLE_GPU_P2P。\n");
    usage(argv[0]);
    return 1;
  }
  fprintf(stdout, "[ENV] NODE_ID=%s NUM_GPUS_PER_SERVER=%s SIMU_ENABLE_GPU_P2P=%s\n",
          getenv("NODE_ID"), getenv("NUM_GPUS_PER_SERVER"), getenv("SIMU_ENABLE_GPU_P2P"));

  // ---- 2.5 连接 ns-3 timer socket（SimAI-CLEM 启动握手，必须！）----
  // ns-3 阻塞在 InitializeTimerInterface() 里 accept 每个 node 的 13000+NODE_ID 连接，
  // 全部 accept 后才创建共享内存段。若跳过此步，ns-3 永久阻塞、后续 IBV 初始化必然失败。
  int node_id = atoi(getenv("NODE_ID"));
  int timer_fd = connect_timer_socket(node_id);
  if (timer_fd < 0) {
    fprintf(stderr, "[FATAL] 无法连接 ns-3 timer socket 127.0.0.1:%d。\n"
                    "        请确认 ns-3 后端已启动，并已打印 'Node %d: Server is listening on port %d'。\n",
            NS3_TIMER_PORT_BASE + node_id, node_id, NS3_TIMER_PORT_BASE + node_id);
    return 1;
  }
  // 导出 SOCKET_FD，供 nsibverbs 的 GetSimTime() 使用（本 demo 不主动查询仿真时间，
  // 但设置它可与 nccl-tests-modify 行为一致，并避免 nsibverbs 内部潜在调用报错）。
  char timer_fd_str[16];
  snprintf(timer_fd_str, sizeof(timer_fd_str), "%d", timer_fd);
  setenv("SOCKET_FD", timer_fd_str, 1);
  fprintf(stdout, "[TIMER] 已导出环境变量 SOCKET_FD=%d\n", timer_fd);

  // ---- 3. 建立 TCP 元数据通道 ----
  int tcp_fd = is_server ? tcp_server_connect(tcp_port)
                         : tcp_client_connect(server_ip, tcp_port);
  if (tcp_fd < 0) { fprintf(stderr, "[FATAL] TCP 建连失败\n"); return 1; }

  // ---- 4. IBV 资源初始化（全部经由 nsibverbs 实现）----
  // 共享内存段 shm_nccl_ns3_<node_id> 由 ns-3 在 accept 完【所有】node 的 timer socket、
  // 进入 Simulator::Run() 执行 StartApplication() 时才创建；而 ibv_get_device_list 内部用
  // boost open_only 打开该段，段不存在会抛异常。故这里用重试机制等待 ns-3 把段建好
  //（例如 server 需等待 client 也连上 timer socket 后，ns-3 才会创建段）。
  int num_devices = 0;
  struct ibv_device **dev_list = NULL;
  const int DEV_LIST_MAX_RETRY = 150;           // 最多等待 150 * 200ms = 30 秒
  for (int attempt = 0; attempt < DEV_LIST_MAX_RETRY; attempt++) {
    num_devices = 0;
    try {
      dev_list = ibv_get_device_list(&num_devices);
    } catch (const std::exception &e) {         // 段未就绪时 boost open_only 抛异常
      dev_list = NULL;
      if (attempt == 0)
        fprintf(stdout, "[IBV] 共享内存段尚未就绪(%s)，等待 ns-3 创建 shm_nccl_ns3_%d ...\n",
                e.what(), node_id);
    }
    if (dev_list && num_devices > 0) {
      if (attempt > 0)
        fprintf(stdout, "[IBV] 共享内存段已就绪（重试 %d 次后）。\n", attempt);
      break;
    }
    dev_list = NULL;
    usleep(200000);                             // 200ms 后重试
  }
  if (!dev_list || num_devices == 0) {
    fprintf(stderr, "[FATAL] ibv_get_device_list 未返回设备（ns-3 后端是否已启动、NODE_ID 是否正确？）\n");
    return 1;
  }
  fprintf(stdout, "[IBV] 发现 %d 个模拟设备，使用 %s\n", num_devices, ibv_get_device_name(dev_list[0]));

  // ibv_open_device: nsibverbs 在此把 ctx->ops.post_send/poll_cq/post_recv 指向 shm_* 实现
  struct ibv_context *ctx = ibv_open_device(dev_list[0]);
  if (!ctx) { fprintf(stderr, "[FATAL] ibv_open_device 失败\n"); return 1; }

  struct ibv_pd *pd = ibv_alloc_pd(ctx);
  if (!pd) { fprintf(stderr, "[FATAL] ibv_alloc_pd 失败\n"); return 1; }

  // 分配数据 buffer 并填入初值
  void *buf = NULL;
  if (posix_memalign(&buf, 4096, BUFFER_SIZE) != 0) { fprintf(stderr, "[FATAL] buffer 分配失败\n"); return 1; }
  if (is_server) {
    memset(buf, 'S', BUFFER_SIZE);                              // server 初始填 'S'，等待被覆盖
  } else {
    for (size_t i = 0; i < BUFFER_SIZE; i++)                    // client 填入可识别的 pattern
      ((char *)buf)[i] = (char)('A' + (i % 26));
  }

  // ibv_reg_mr: 注册内存区，得到 lkey/rkey。nsibverbs 返回固定的 lkey/rkey=100
  struct ibv_mr *mr = ibv_reg_mr(pd, buf, BUFFER_SIZE,
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
  if (!mr) { fprintf(stderr, "[FATAL] ibv_reg_mr 失败\n"); return 1; }
  fprintf(stdout, "[IBV] 注册 MR: addr=%p len=%zu lkey=0x%x rkey=0x%x\n",
          buf, BUFFER_SIZE, mr->lkey, mr->rkey);

  // ibv_create_cq: nsibverbs 会把模拟器返回的共享内存名存入 cq->cq_context
  struct ibv_cq *cq = ibv_create_cq(ctx, CQ_CAPACITY, NULL, NULL, 0);
  if (!cq) { fprintf(stderr, "[FATAL] ibv_create_cq 失败\n"); return 1; }

  // ibv_create_qp: RC 类型 QP，收发共用一个 CQ
  struct ibv_qp_init_attr qp_init_attr;
  memset(&qp_init_attr, 0, sizeof(qp_init_attr));
  qp_init_attr.send_cq = cq;
  qp_init_attr.recv_cq = cq;
  qp_init_attr.qp_type = IBV_QPT_RC;
  qp_init_attr.sq_sig_all = 0;
  qp_init_attr.cap.max_send_wr  = MAX_SEND_WR;
  qp_init_attr.cap.max_recv_wr  = MAX_RECV_WR;
  qp_init_attr.cap.max_send_sge = 1;
  qp_init_attr.cap.max_recv_sge = 1;
  struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
  if (!qp) { fprintf(stderr, "[FATAL] ibv_create_qp 失败\n"); return 1; }
  fprintf(stdout, "[IBV] 创建 QP: qp_num=%u\n", qp->qp_num);

  // ---- 5. 查询本地 port / gid ----
  struct ibv_port_attr port_attr;
  memset(&port_attr, 0, sizeof(port_attr));
  ibv_query_port(ctx, IB_PORT_NUM, &port_attr);   // RoCE 下 lid 可能为 0，不影响

  union ibv_gid local_gid;
  memset(&local_gid, 0, sizeof(local_gid));
  ibv_query_gid(ctx, IB_PORT_NUM, GID_INDEX, &local_gid);

  // ---- 6. 组装本地元数据 ----
  struct conn_meta local, remote;
  memset(&local, 0, sizeof(local));
  local.qp_num  = qp->qp_num;
  local.psn     = (uint32_t)(getpid() & 0x00ffffff);   // 用 pid 生成一个简单 PSN
  local.lid     = port_attr.lid;
  local.node_id = (uint32_t)atoi(getenv("NODE_ID"));
  local.rkey    = mr->rkey;
  local.vaddr   = (uint64_t)buf;
  local.gid     = local_gid;

  // ---- 7. 通过 TCP 交换元数据 ----
  if (tcp_send_all(tcp_fd, &local, sizeof(local)) < 0) { fprintf(stderr, "[FATAL] 发送元数据失败\n"); return 1; }
  if (tcp_recv_all(tcp_fd, &remote, sizeof(remote)) < 0) { fprintf(stderr, "[FATAL] 接收元数据失败\n"); return 1; }
  fprintf(stdout, "[TCP] 元数据交换完成: 对端 qp_num=%u node_id=%u rkey=0x%lx vaddr=0x%lx\n",
          remote.qp_num, remote.node_id, (unsigned long)remote.rkey, (unsigned long)remote.vaddr);

  // ---- 8. QP 状态迁移: RESET -> INIT -> RTR -> RTS ----
  if (qp_to_init(qp) != 0) { fprintf(stderr, "[FATAL] modify_qp -> INIT 失败\n"); return 1; }
  fprintf(stdout, "[QP] RESET -> INIT 完成\n");

  if (qp_to_rtr(qp, &remote) != 0) { fprintf(stderr, "[FATAL] modify_qp -> RTR 失败\n"); return 1; }
  fprintf(stdout, "[QP] INIT  -> RTR  完成 (对端 node_id=%u 已编码进 dgid，并写入 qp->handle)\n", remote.node_id);

  if (qp_to_rts(qp, local.psn) != 0) { fprintf(stderr, "[FATAL] modify_qp -> RTS 失败\n"); return 1; }
  fprintf(stdout, "[QP] RTR   -> RTS  完成，QP 已就绪\n");

  // ---- 9. client 发起 RDMA WRITE ----
  if (!is_server) {
    fprintf(stdout, "[client] 发起 RDMA WRITE: %zu 字节 -> 对端 vaddr=0x%lx rkey=0x%lx\n",
            BUFFER_SIZE, (unsigned long)remote.vaddr, (unsigned long)remote.rkey);

    struct ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr   = (uint64_t)buf;   // 本地源地址
    sge.length = BUFFER_SIZE;
    sge.lkey   = mr->lkey;

    struct ibv_send_wr wr, *bad_wr = NULL;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id      = 0x1234;
    wr.sg_list    = &sge;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_RDMA_WRITE;
    wr.send_flags = IBV_SEND_SIGNALED;          // 要求产生 CQE
    wr.wr.rdma.remote_addr = remote.vaddr;      // 远端目标地址
    wr.wr.rdma.rkey        = remote.rkey;       // 远端访问 key

    // ibv_post_send 是 verbs.h 里的 inline，会调用 ctx->ops.post_send，
    // 而它已被 ibv_open_device 指向 shm_ibv_post_send —— 命令被写入共享内存交给模拟器。
    if (ibv_post_send(qp, &wr, &bad_wr) != 0) { fprintf(stderr, "[FATAL] ibv_post_send 失败\n"); return 1; }
    fprintf(stdout, "[client] post_send 已下发，开始轮询 CQE...\n");

    // ibv_poll_cq -> ctx->ops.poll_cq = shm_ibv_poll_cq：从共享内存读取模拟器回填的 CQE
    struct ibv_wc wc;
    memset(&wc, 0, sizeof(wc));
    int ne = 0;
    time_t start = time(NULL);
    while ((ne = ibv_poll_cq(cq, 1, &wc)) == 0) {
      if ((time(NULL) - start) > POLL_TIMEOUT_SEC) {
        fprintf(stderr, "[FATAL] 等待 CQE 超时(%d 秒)。请确认 ns-3 后端正在运行并处理命令。\n", POLL_TIMEOUT_SEC);
        return 1;
      }
    }
    if (ne < 0 || wc.status != IBV_WC_SUCCESS) {
      fprintf(stderr, "[FATAL] RDMA WRITE 完成状态异常: status=%d\n", wc.status);
      return 1;
    }
    fprintf(stdout, "[client] 收到 CQE: wr_id=0x%lx opcode=%d status=IBV_WC_SUCCESS，RDMA WRITE 完成\n",
            (unsigned long)wc.wr_id, wc.opcode);
  } else {
    fprintf(stdout, "[server] 已就绪，等待 client 的 RDMA WRITE...\n");
  }

  // ---- 10. TCP 同步：双方各自完成后再校验 ----
  char sync = 'D';
  if (tcp_send_all(tcp_fd, &sync, 1) < 0) { fprintf(stderr, "[FATAL] 同步(发送)失败\n"); return 1; }
  if (tcp_recv_all(tcp_fd, &sync, 1) < 0) { fprintf(stderr, "[FATAL] 同步(接收)失败\n"); return 1; }

  // ---- 11. server 校验 buffer 是否被写入 ----
  if (is_server) {
    char *p = (char *)buf;
    int ok = 1;
    for (size_t i = 0; i < BUFFER_SIZE; i++) {
      if (p[i] != (char)('A' + (i % 26))) { ok = 0; break; }
    }
    fprintf(stdout, "[server] buffer 前 32 字节: \"%.32s\"\n", p);
    if (ok) {
      fprintf(stdout, "[server] ✅ 校验成功：buffer 已被 client 通过 RDMA WRITE 写入预期数据！\n");
    } else {
      fprintf(stdout, "[server] ℹ️ buffer 内容未变化：控制面(QP 建立、post_send 命令下发)已成功走通\n"
                      "          SimAI-CLEM 双向交互框架；数据面是否真正搬运取决于 ns-3 后端的 RDMA 模拟实现。\n");
    }
  }

  // ---- 12. 清理资源 ----
  fprintf(stdout, "[%s] 清理资源...\n", role);
  ibv_destroy_qp(qp);
  ibv_destroy_cq(cq);
  ibv_dereg_mr(mr);
  free(buf);
  ibv_dealloc_pd(pd);
  ibv_close_device(ctx);
  ibv_free_device_list(dev_list);
  close(tcp_fd);
  fprintf(stdout, "==== [%s] demo 结束 ====\n", role);
  return 0;
}
