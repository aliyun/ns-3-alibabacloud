# SimAI-CLEM RDMA_demo

A minimal, **GPU-free** and **RNIC-free** two-process **RDMA WRITE** demonstration.

This is a self-contained teaching example that illustrates the design philosophy of SimAI-CLEM's
core **bidirectional interaction framework**: how an ordinary application's IBV Verbs calls
are intercepted by `nsibverbs` and forwarded to the ns-3 network simulator through shared
memory — instead of going to a physical RDMA NIC.

> The demo reuses **exactly the same Verbs implementation** as the real SimAI-CLEM
> (`nccl_hack_rdma/src/nsibverbs/`), but strips away the NCCL / CUDA / GPU dependencies.
> So even without a GPU, you can read, build and understand how SimAI-CLEM redirects RDMA traffic
> into the simulator.

---

## 1. Background

In the full SimAI-CLEM system, native NCCL runs on real GPUs. Every RDMA operation NCCL issues
(through the IBV Verbs API) is intercepted by `nsibverbs` and redirected to the ns-3
simulator rather than to a physical RNIC. Reproducing that end-to-end normally requires GPUs.

This demo removes the GPU/NCCL layer and keeps only the **essential interaction loop**:

```
   Application (this demo)              nsibverbs                       ns-3 simulator
   ───────────────────────             ─────────                       ──────────────
   ibv_open_device()     ─┐
   ibv_alloc_pd()         │   intercepted & translated into an
   ibv_reg_mr()           │   IbvCommand, then written into the
   ibv_create_cq()        ├──────────►  boost::interprocess  ─────────►  consumes commands,
   ibv_create_qp()        │             shared-memory queue              simulates the RDMA
   ibv_modify_qp()        │             (cmd_queue)                      transfer, and writes
   ibv_post_send(WRITE)   │                                              back responses / CQEs
   ibv_poll_cq()         ─┘  ◄─────────  response / CQE  ◄─────────────
```

This is the **"bidirectional interaction"**: a Verbs call on the application side pushes a
command down to the simulator; once the simulator finishes emulating it, the response / CQE
is pushed back up to the application side.

Two processes act as the two communicating endpoints:

- **server** (`-s`): the *target* of the RDMA WRITE. It registers a buffer and waits to be written.
- **client** (`-c`): the *initiator*. It RDMA-WRITEs its local buffer into the server's buffer.

All out-of-band metadata (QP number, GID, rkey, remote buffer address, NODE_ID) is exchanged
over a plain **TCP socket** — exactly the classic RDMA connection-setup ("handshake") pattern.

---

## 2. Directory layout

```
RDMA_demo/
├── nsibverbs/               # IBV Verbs implementation, copied verbatim from
│   ├── command.cc           #   nccl_hack_rdma/src/nsibverbs/  (7 files, unmodified)
│   ├── cq.cc
│   ├── device.cc
│   ├── event.cc
│   ├── mr.cc
│   ├── pd.cc
│   └── qp.cc
├── include/                 # Headers
│   ├── verbs.h              #   copied from nccl_hack_rdma/src/include/verbs.h (see §7)
│   ├── command.h            #   copied from nccl_hack_rdma/src/include/command.h (see §7)
│   └── debug.h              #   a minimal stub replacing NCCL's debug.h (see §7)
├── rdma_write_demo.cc       # The two-process RDMA WRITE demo (server & client in one binary)
├── Makefile
├── .gitignore              # Build artifacts ignored by git (build/, binary, pollTimeVector.txt)
└── README.md                # This file
```

---

## 3. Dependencies

| Required | Notes |
| :------- | :---- |
| g++ (C++14) | Any reasonably modern GCC/Clang |
| Boost.Interprocess | **Headers only** (`/usr/include/boost/interprocess`). No compiled Boost libs needed |
| GNU make | |
| pthread, librt | Standard on Linux (`-pthread -lrt`) |

**NOT required:** GPU, CUDA, a physical RNIC, or the system `libibverbs` / `rdma-core`.
The Verbs API is fully provided by the bundled `nsibverbs/` + `include/verbs.h`.

---

## 4. Build

```bash
cd RDMA_demo
make
```

On success this produces the executable `./rdma_write_demo`. To clean up:

```bash
make clean
```

> A single harmless warning may appear (`GetCurrentTime` returns a local address). It comes
> from the original `nsibverbs/command.cc` and is never exercised by this demo.

---

## 5. Run

### 5.1 Prerequisites

`nsibverbs` does not talk to a real NIC — it talks to the **ns-3 simulator backend** through
a shared-memory segment named `shm_nccl_ns3_<NODE_ID>`. Therefore, before running the demo you
must have the ns-3 backend running so that this segment exists.

> This demo lives **inside** the `ns-3-alibabacloud` repository
> ([`aliyun/ns-3-alibabacloud`](https://github.com/aliyun/ns-3-alibabacloud), branch
> [`dev/clem`](https://github.com/aliyun/ns-3-alibabacloud/tree/dev/clem)),
> alongside the ns-3 backend in the sibling `simulation/` directory. Build the backend first
> (see the top-level project README), then start it before running the demo.

### 5.2 Step 1 — Start the ns-3 backend (Terminal 1)

From the **root of the `ns-3-alibabacloud` repository** (the directory that also contains
this `RDMA_demo/`), start the backend with **at least 2 nodes** (node 0 for the server,
node 1 for the client):

```bash
cd simulation/
./ns3 run 'scratch/QpReuseSimInfra mix/incast/config_example.sh --numnodes=2'
```

### 5.3 Step 2 — Run the server process (Terminal 2)

```bash
cd RDMA_demo
export NODE_ID=0                 # this process = simulator node 0
export NUM_GPUS_PER_SERVER=8
export SIMU_ENABLE_GPU_P2P=false # MUST be set; nsibverbs reads it unconditionally
./rdma_write_demo -s -p 18515
```

### 5.4 Step 3 — Run the client process (Terminal 3)

```bash
cd RDMA_demo
export NODE_ID=1                 # this process = simulator node 1
export NUM_GPUS_PER_SERVER=8
export SIMU_ENABLE_GPU_P2P=false
./rdma_write_demo -c 127.0.0.1 -p 18515
```

> The server and client may run on the same host (hence `127.0.0.1`); what distinguishes them
> at the simulator level is the `NODE_ID` environment variable.

### 5.5 Environment variables

| Variable | Required | Meaning |
| :------- | :------- | :------ |
| `NODE_ID` | Yes | This process's node id in the simulator (server=0, client=1) |
| `NUM_GPUS_PER_SERVER` | Yes | GPUs per server; used by `nsibverbs` for same-server detection |
| `SIMU_ENABLE_GPU_P2P` | Yes | Enables the `/proc/<pid>/mem` P2P fast-path. **Must be set** (e.g. `false`), otherwise `nsibverbs` dereferences a NULL `getenv()` result |
| `RDMA_DEMO_VERBOSE` | No | If set, prints `nsibverbs` internal `INFO` logs (from `include/debug.h`) |

---

## 6. Expected output

**Client** (initiator):

```
==== SimAI-CLEM RDMA_demo [client] 启动 ====
[ENV] NODE_ID=1 NUM_GPUS_PER_SERVER=8 SIMU_ENABLE_GPU_P2P=false
[TCP] 已连接到 server 127.0.0.1:18515
[IBV] 发现 N 个模拟设备，使用 ns3_qbbdev_0
[IBV] 注册 MR: addr=0x... len=4096 lkey=0x64 rkey=0x64
[IBV] 创建 QP: qp_num=...
[TCP] 元数据交换完成: 对端 qp_num=... node_id=0 rkey=0x64 vaddr=0x...
[QP] RESET -> INIT 完成
[QP] INIT  -> RTR  完成 (对端 node_id=0 已编码进 dgid，并写入 qp->handle)
[QP] RTR   -> RTS  完成，QP 已就绪
[client] 发起 RDMA WRITE: 4096 字节 -> 对端 vaddr=0x... rkey=0x64
[client] post_send 已下发，开始轮询 CQE...
[client] 收到 CQE: wr_id=0x1234 opcode=0 status=IBV_WC_SUCCESS，RDMA WRITE 完成
==== [client] demo 结束 ====
```

**Server** (target):

```
==== SimAI-CLEM RDMA_demo [server] 启动 ====
[TCP] 正在端口 18515 监听，等待对端连接...
[TCP] 对端已连接。
[QP] RTR   -> RTS  完成，QP 已就绪
[server] 已就绪，等待 client 的 RDMA WRITE...
[server] buffer 前 32 字节: "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEF"
[server] ✅ 校验成功：buffer 已被 client 通过 RDMA WRITE 写入预期数据！
==== [server] demo 结束 ====
```

> **Note on the data plane:** the *control plane* (device open, QP creation/state transitions,
> `post_send` command dispatch, CQE polling) always flows through the SimAI-CLEM framework shown above.
> Whether the server's buffer bytes are *physically* updated depends on how the ns-3 backend
> emulates the actual data movement. If the buffer is unchanged, the demo prints an informational
> message instead of failing — the interaction framework itself has still been exercised end to end.

---

## 7. What was changed vs. the original `nsibverbs`

The seven `nsibverbs/*.cc` files are copied **verbatim** (zero changes). Only the headers were
minimally adapted so the code can compile **without NCCL, CUDA, or the system RDMA headers**:

| File | Change | Reason |
| :--- | :----- | :----- |
| `include/verbs.h` | Removed `#include <infiniband/verbs_api.h>`; added minimal definitions for `IBV_ACCESS_OPTIONAL_FIRST`, `ibv_flow_action_esp_keymat/replay`, `ibv_advise_mr_advice` | The system RDMA header is absent in a GPU-free/RDMA-free environment, and `verbs.h` uses none of its real features |
| `include/command.h` | Replaced `#include "core.h"` with `<unistd.h>` + `<stdlib.h>` | `core.h` pulls in `nccl.h` / `checks.h` / `cudawrap.h` / CUDA — far too heavy for a standalone demo |
| `include/debug.h` | A minimal stub (not copied from NCCL) providing `WARN` / `INFO` macros | The original depends on `nccl.h` and `ncclDebugLog()` |

Everything else (the shared-memory command protocol, `context->ops` dispatch, the
`dgid → remote_node_id` convention) is identical to the real SimAI-CLEM.

---

## 8. Code walkthrough (`rdma_write_demo.cc`)

The `main()` flow mirrors a textbook RC RDMA WRITE program:

1. **Parse args & check env** — role (`-s`/`-c`), TCP port; verify `NODE_ID` etc.
2. **TCP handshake channel** — `tcp_server_connect()` / `tcp_client_connect()`.
3. **IBV init (all via `nsibverbs`)** — `ibv_get_device_list` → `ibv_open_device`
   (this is where `context->ops.post_send/poll_cq` get pointed at the `shm_*` implementations)
   → `ibv_alloc_pd` → `ibv_reg_mr` → `ibv_create_cq` → `ibv_create_qp`.
4. **Query local port/GID**, assemble `conn_meta`.
5. **Exchange `conn_meta` over TCP** — the out-of-band handshake.
6. **QP state machine** — `RESET → INIT → RTR → RTS` via `ibv_modify_qp`.
   - In `qp_to_rtr()`, the peer's `node_id` is encoded into the **high 32 bits** of
     `ah_attr.grh.dgid.global.interface_id`. This is the SimAI-CLEM convention: `nsibverbs`'s
     `ibv_modify_qp` extracts it back into `qp->handle`, which later tells `post_send`
     which simulator node is the destination.
7. **Client posts `IBV_WR_RDMA_WRITE`** — `ibv_post_send()` → `shm_ibv_post_send()` →
   an `IBV_POST_SEND` command into the shared-memory queue.
8. **Client polls the CQ** — `ibv_poll_cq()` → `shm_ibv_poll_cq()` reads the CQE that the
   simulator wrote back.
9. **Sync over TCP, server verifies its buffer, then both sides clean up.**

---

## 9. FAQ / Troubleshooting

- **`ibv_get_device_list` returns nothing / aborts**
  The ns-3 backend is not running, or `shm_nccl_ns3_<NODE_ID>` does not exist, or `NODE_ID`
  does not match a node the backend created. Start the backend first (§5.2).

- **Segfault right after `post_send`**
  `SIMU_ENABLE_GPU_P2P` is not set. `nsibverbs` calls `strcmp(getenv("SIMU_ENABLE_GPU_P2P"), "true")`
  unconditionally; a NULL result crashes. Always export it (§5.5). The demo checks this up front.

- **`[server] buffer 内容未变化`**
  Control plane worked; the backend did not physically move the bytes. See the note in §6.

- **Build fails on `<infiniband/verbs_api.h>` or `core.h`**
  You are likely compiling against the original `nccl_hack_rdma` headers instead of the ones in
  `RDMA_demo/include/`. The Makefile already adds `-Iinclude`; do not add NCCL's include path.
