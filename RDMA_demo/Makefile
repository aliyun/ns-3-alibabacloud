# ============================================================================
# RDMA_demo Makefile
#   编译 CLEM 的两进程 RDMA WRITE 演示程序。
#   Verbs API 实现全部来自本目录 nsibverbs/（copy 自 nccl_hack_rdma/src/nsibverbs/），
#   不依赖系统 libibverbs、不依赖 CUDA、不依赖 GPU。
#
#   依赖：g++（支持 C++14）、Boost（interprocess，头文件即可）、GNU make、pthread、librt。
# ============================================================================

CXX      := g++
# 说明：nsibverbs 原代码存在两处已知且对本 demo 无害的告警
#   (GetCurrentTime 返回局部地址、post_send_data_transport 非 READ/WRITE 分支无返回值)，
#   为保持“原样 copy”，此处不修改源码，仅在编译时抑制这两类告警以保持输出整洁。
CXXFLAGS := -std=c++14 -O2 -Iinclude -pthread -Wno-return-type -Wno-unused-result
LDFLAGS  := -pthread -lrt

NSIBVERBS_DIR := nsibverbs
BUILD_DIR     := build
TARGET        := rdma_write_demo

# 源文件与目标文件
NSIBVERBS_SRCS := $(wildcard $(NSIBVERBS_DIR)/*.cc)
NSIBVERBS_OBJS := $(patsubst $(NSIBVERBS_DIR)/%.cc,$(BUILD_DIR)/%.o,$(NSIBVERBS_SRCS))
DEMO_OBJ       := $(BUILD_DIR)/rdma_write_demo.o
ALL_OBJS       := $(NSIBVERBS_OBJS) $(DEMO_OBJ)

.PHONY: all clean help

all: $(TARGET)

# 链接生成可执行文件
$(TARGET): $(ALL_OBJS)
	$(CXX) $(ALL_OBJS) -o $@ $(LDFLAGS)
	@echo ""
	@echo "==== 编译成功: ./$(TARGET) ===="
	@echo "运行前请阅读 README.md（需先启动 ns-3 后端并设置 NODE_ID 等环境变量）。"
	@echo "查看用法: make help"

# demo 主程序（显式规则，优先于下面的模式规则）
$(DEMO_OBJ): rdma_write_demo.cc | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# nsibverbs 实现文件（模式规则）
$(BUILD_DIR)/%.o: $(NSIBVERBS_DIR)/%.cc | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

help:
	@./$(TARGET) -h || true

clean:
	rm -rf $(BUILD_DIR) $(TARGET) pollTimeVector.txt
	@echo "已清理构建产物。"
