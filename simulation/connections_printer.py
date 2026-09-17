import re

# 读取日志文件
with open('/etc/CLEM/output_print_many.log', 'r') as file:
    log_content = file.readlines()

# 用于存储连接信息的字典
connections = {}

# 正则表达式匹配Connection: QP(node1,qpnum1) <==> QP(node2,qpnum2)
connection_pattern = re.compile(r'Connection: QP\((\d+),(\d+)\) <==> QP\((\d+),(\d+)\)')

for line in log_content:
    match = connection_pattern.search(line)
    if match:
        node1, qp_num1, node2, qp_num2 = map(int, match.groups())
        
        # 只统计不同节点之间的连接
        if node1 != node2:
            # 确保node1 < node2
            if node1 < node2:
                continue
            
            # 使用元组 (node1, node2) 作为键
            key = (node1, node2)
            
            # 如果键不存在，则初始化为0
            if key not in connections:
                connections[key] = 0
            
            # 增加连接计数
            connections[key] += 1

# 打印结果
total_count = 0
for (node1, node2), count in sorted(connections.items()):
    total_count += count
    print(f"Node {node1} and Node {node2} have {count} connections")

print("Avg channels:", total_count / 16)


for line in log_content:
    match = connection_pattern.search(line)
    if match:
        node1, qp_num1, node2, qp_num2 = map(int, match.groups())
        
        # 只统计不同节点之间的连接
        if node1 < node2 and node1 == 0 and node2 >= 8:
            print(line.strip())
            