# CLEM ns-3 Network Backend

This repository is the **network simulator** component of **CLEM** (Collective communication
Library EMulator). It is maintained on the
[`feat/ipc-middleware`](https://github.com/aliyun/ns-3-alibabacloud/tree/feat/ipc-middleware)
branch. It provides an ns-3-based backend that emulates the point-to-point (P2P) communication of
a Collective Communications Library (CCL), together with a **GPU-free** demonstration
([`RDMA_demo/`](RDMA_demo/)) of how an application interacts with this backend.

---

## 1. What this ns-3 version is for

CLEM is a CCL emulator that runs a **vanilla CCL directly on GPU hardware**, while **intercepting
the CCL's P2P communication primitives** and **redirecting them to a network simulator** that
reproduces their behavior. This makes it possible to study large-scale collective communication
under realistic, non-stationary network conditions without deploying a full physical cluster.

CLEM is built from three cooperating components:

- **Modified CCL (`nccl_hack_rdma`)** — hosts the **API Navigator**, which captures the CCL's
  RDMA/P2P calls (e.g. `ibv_post_send`) and reroutes them to the simulator middleware instead of a
  physical RNIC.
- **This ns-3 backend (`simulator-network`)** — receives the redirected calls and **simulates the
  behavior of those P2P primitives**: the actual data transfer, congestion control, and timing.
- **Instrumented tests (`nccl-tests-modify`)** — drive the collective workloads and expose the
  simulated network time back to the application.

**This repository is the ns-3 backend — it exists precisely to simulate the intercepted P2P
primitives on behalf of CLEM.**

To let ns-3 understand and respond to IBV Verbs-style calls coming from the CCL, we extend ns-3
with a set of custom classes:

- `IbvQP` — models Queue Pairs for reliable, connection-oriented communication.
- `IbvCQ` — implements Completion Queues for asynchronous operation notification.
- `IbvInterface` / `IbvInterfaceHelper` — provide the abstraction layer for virtualized NIC
  functionality.

Together, these components allow ns-3 to parse, execute, and respond to IBV-style communication
calls issued by upper-layer applications such as NCCL. For the detailed design rationale and class
relationships, refer to the inline code comments and the material under [`docs/`](docs/).

---

## 2. A GPU-free way in: `RDMA_demo`

The **full** CLEM pipeline requires the CCL process to run on **real GPUs**. To let users read,
build, and understand CLEM's core **bidirectional interaction framework** even *without* a GPU (or a
physical RNIC), this repository ships a self-contained teaching example under
[`RDMA_demo/`](RDMA_demo/).

`RDMA_demo` is a minimal, **GPU-free** and **RNIC-free** two-process **RDMA WRITE** program. It
**reuses exactly the same Verbs implementation as the real CLEM** (`nsibverbs`, copied verbatim
from `nccl_hack_rdma/src/nsibverbs/`) but strips away the NCCL / CUDA / GPU dependencies. It
illustrates the essential interaction loop: an application's IBV Verbs call is intercepted by
`nsibverbs`, translated into a command, and pushed down to the ns-3 simulator through shared
memory; once the simulator finishes emulating the transfer, the response / completion (CQE) is
pushed back up to the application.

```
   Application (the demo)               nsibverbs                       ns-3 simulator
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

This is the **"bidirectional interaction"** at the heart of CLEM — the same mechanism the real
system uses, only with the GPU/NCCL layer removed.

---

## 3. Build and run the ns-3 backend

All commands below are run from the **root of this repository** (the directory that contains
`simulation/` and `RDMA_demo/`).

### 3.1 Build

```bash
cd simulation/
source build.sh          # see simulation/build.sh for details
```

### 3.2 Run

Start the ns-3 simulation engine. It **must be running before** any CCL / demo process, so that the
IPC handshake and the shared-memory segments are ready:

```bash
cd simulation/
./ns3 run 'scratch/QpReuseSimInfra {CONFIG_FILE_PATH} --numnodes={NUM_RANKS}'
# For example:
./ns3 run 'scratch/QpReuseSimInfra mix/incast/config_example.sh --numnodes=2'
```

- `{CONFIG_FILE_PATH}` — path to a configuration script, relative to `simulation/`. A fully
  documented example is `mix/incast/config_example.sh`, which covers tunable parameters such as
  topology and NIC settings.
- `{NUM_RANKS}` — number of ranks / nodes in the task. Use **at least `2`** for `RDMA_demo`
  (node 0 = server, node 1 = client).

---

## 4. `RDMA_demo`: code architecture and how to run

### 4.1 Code architecture

```
RDMA_demo/
├── nsibverbs/               # IBV Verbs implementation, copied verbatim from
│   ├── command.cc           #   nccl_hack_rdma/src/nsibverbs/  (7 files, unmodified)
│   ├── cq.cc  device.cc  event.cc  mr.cc  pd.cc  qp.cc
├── include/                 # Headers (verbs.h / command.h adapted, debug.h is a stub)
├── rdma_write_demo.cc       # The two-process RDMA WRITE demo (server & client in one binary)
├── Makefile
└── README.md                # Full details: build, expected output, walkthrough, FAQ
```

The demo builds into a single executable that plays both endpoints:

- **server** (`-s`) — the *target* of the RDMA WRITE. It registers a buffer and waits to be written.
- **client** (`-c`) — the *initiator*. It RDMA-WRITEs its local buffer into the server's buffer.

All out-of-band metadata (QP number, GID, rkey, remote buffer address, `NODE_ID`) is exchanged over
a plain **TCP socket** — the classic RDMA connection-setup ("handshake") pattern — while the actual
RDMA operation flows through `nsibverbs` into the ns-3 backend.

### 4.2 Run the demo

**Prerequisite:** the ns-3 backend must be running (see §3), because `nsibverbs` talks to it through
a shared-memory segment named `shm_nccl_ns3_<NODE_ID>` rather than to a real NIC.

**Terminal 1 — start the ns-3 backend** (at least 2 nodes):

```bash
cd simulation/
./ns3 run 'scratch/QpReuseSimInfra mix/incast/config_example.sh --numnodes=2'
```

**Terminal 2 — build and run the server** (target, node 0):

```bash
cd RDMA_demo
make                                   # build once; produces ./rdma_write_demo
export NODE_ID=0                       # this process = simulator node 0
export NUM_GPUS_PER_SERVER=8
export SIMU_ENABLE_GPU_P2P=false       # MUST be set; nsibverbs reads it unconditionally
./rdma_write_demo -s -p 18515
```

**Terminal 3 — run the client** (initiator, node 1):

```bash
cd RDMA_demo
export NODE_ID=1                       # this process = simulator node 1
export NUM_GPUS_PER_SERVER=8
export SIMU_ENABLE_GPU_P2P=false
./rdma_write_demo -c 127.0.0.1 -p 18515
```

The server and client may run on the same host (hence `127.0.0.1`); what distinguishes them at the
simulator level is the `NODE_ID` environment variable.

| Variable | Required | Meaning |
| :------- | :------- | :------ |
| `NODE_ID` | Yes | This process's node id in the simulator (server = 0, client = 1) |
| `NUM_GPUS_PER_SERVER` | Yes | GPUs per server; used by `nsibverbs` for same-server detection |
| `SIMU_ENABLE_GPU_P2P` | Yes | Enables the `/proc/<pid>/mem` P2P fast-path. **Must be set** (e.g. `false`), otherwise `nsibverbs` dereferences a NULL `getenv()` result |
| `RDMA_DEMO_VERBOSE` | No | If set, prints `nsibverbs` internal `INFO` logs |

> For expected output, a line-by-line code walkthrough of `rdma_write_demo.cc`, and
> troubleshooting / FAQ, see [`RDMA_demo/README.md`](RDMA_demo/README.md).

---

## License

ns-3 is released under the GNU GPLv2. See [`LICENSE`](LICENSE) for the full terms.
