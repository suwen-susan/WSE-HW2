# 快速入门指南

## 处理完整数据集（8.8M 文档）

### 步骤 1: 编译所有程序

```bash
g++ -std=c++17 src/indexer.cpp -o indexer.exe -I./include -O2
g++ -std=c++17 src/merger.cpp -o merger.exe -I./include -O2
g++ -std=c++17 src/index_inspector.cpp -o inspector.exe -I./include -O2
g++ -std=c++17 src/querier.cpp -o querier.exe -I./include -O2
```

### 步骤 2: Phase 1 - 生成 posting 流（预计 5-10 分钟）

```bash
# 参数说明：
# - data/collection.tsv: 输入数据
# - output: 输出目录
# - 4: 每个分片 4GB（可选，默认 2GB）

indexer.exe data/collection.tsv output 4
```

**预期输出**：
- `output/doc_table.txt`: 文档表
- `output/postings_part_0.tsv`, `postings_part_1.tsv`, ...: 分片文件

### 步骤 3: 使用 msort 全局排序（预计 5-10 分钟）

```bash
Git Bash/Cygwin 使用 GNU sort（提供更稳定性能）
LC_ALL=C sort -t $'\t' -k1,1 -k2,2n \
  -S 70% --parallel="$(nproc)" \
  --temporary-directory='output' \
  output/postings_part_*.tsv \
  -o output/postings_sorted.tsv
```

**关键参数**：
- `-t '\t'`: 字段分隔符为 TAB
- `-k 1,1`: 主键为第1列（term），字典序
- `-k 2,2n`: 次键为第2列（docID），数值升序

### 步骤 4: Phase 2 - 合并为压缩索引（预计 3-5 分钟）

```bash
merger.exe output/postings_sorted.tsv index
```

**预期输出**：
- `index/postings.docids.bin`: 压缩的 docID 序列
- `index/postings.freqs.bin`: 压缩的 frequency 序列
- `index/lexicon.tsv`: 词典
- `index/stats.txt`: 统计信息

### 步骤 5: 验证索引

```bash
# 查看统计信息和词典概览
inspector.exe index

# 查看具体词项的倒排表
inspector.exe index computer science algorithm

### 步骤 6: Phase 3 - 查询处理器（交互式）

```bash
querier.exe index output/doc_table.txt --mode=or --k=10
```

示例交互：
```text
> machine learning
> /and deep learning
> /quit
```

### 一键脚本（Windows）

```bash
.\n+run_all.bat
```
脚本会自动编译四个可执行文件，按需执行三阶段并启动查询器；当 msort 不可用时会自动回退到 PowerShell 或 GNU sort。
```

## 常用命令

### 查看进度

```bash
# Phase 1 运行时会每处理 10,000 文档打印一次进度
# Phase 2 运行时会每处理 10,000,000 postings 打印一次进度
```

### 检查中间文件

```bash
# 查看生成了多少个分片
dir output\postings_part_*.tsv  # Windows
ls output/postings_part_*.tsv   # Linux/macOS

# 查看分片文件大小
dir output  # Windows
ls -lh output  # Linux/macOS

# 查看排序后文件的行数
wc -l output/postings_sorted.tsv  # Linux/macOS
```

### 查看词典内容

```bash
# 前 50 个词项
head -50 index/lexicon.tsv  # Linux/macOS
powershell -Command "Get-Content index\lexicon.tsv | Select-Object -First 50"  # Windows

# 查找特定词
grep "algorithm" index/lexicon.tsv  # Linux/macOS
findstr "algorithm" index\lexicon.tsv  # Windows
```

### 查看统计信息

```bash
cat index/stats.txt  # Linux/macOS
type index\stats.txt  # Windows
```

## 故障排除

### 问题 1: indexer.exe 编译失败
**解决**：确保使用 C++17 标准
```bash
g++ --version  # 确认版本 >= 7.0
g++ -std=c++17 src/indexer.cpp -o indexer.exe -I./include
```

### 问题 2: 内存不足
**解决**：减小分片大小
```bash
indexer.exe data/collection.tsv output 1  # 每个分片 1GB
```

### 问题 3: msort 不支持通配符
**解决**：显式列出所有文件或使用脚本
```bash
# 创建文件列表
ls output/postings_part_*.tsv > files.txt

# 批量处理（依赖 msort 的具体用法）
```

### 问题 4: 排序后文件为空
**解决**：检查 msort 命令和文件路径
```bash
# 确认分片文件存在
dir output\postings_part_*.tsv

# 手动测试小文件
msort -t '\t' -k 1,1 -k 2,2n output/postings_part_0.tsv > test_sorted.tsv
type test_sorted.tsv  # 检查是否有内容
```

### 问题 5: merger.exe 报错 "Cannot open input file"
**解决**：确认排序文件存在且路径正确
```bash
dir output\postings_sorted.tsv  # 确认文件存在
head output/postings_sorted.tsv  # 确认格式正确（term<TAB>docID<TAB>tf）
```

## 性能优化建议

### 1. 使用 SSD
- SSD 比 HDD 快 3-5 倍
- 将 output 和 index 目录放在 SSD 上

### 2. 调整分片大小
```bash
# 内存充足时使用更大的分片（减少文件数）
indexer.exe data/collection.tsv output 8  # 8GB/分片
```

### 3. 使用 O2 优化编译
```bash
g++ -std=c++17 src/indexer.cpp -o indexer.exe -I./include -O2
```

### 4. 并行处理（高级）
- Phase 1 可以分段处理数据集并行运行（需手动分割输入）
- msort 支持多线程（查看 msort 文档）

## 预期结果（8.8M 文档数据集）

### Phase 1
- **文件数**：~50-100 个分片（取决于分片大小）
- **总大小**：~3-5 GB
- **时间**：SSD 5-10 分钟，HDD 15-25 分钟

### msort
- **输出大小**：~3-5 GB（与输入相同）
- **时间**：5-10 分钟（取决于 msort 实现和磁盘速度）

### Phase 2
- **文件数**：4 个
- **总大小**：~1-1.5 GB（压缩后）
- **时间**：SSD 3-5 分钟，HDD 10-15 分钟
- **压缩率**：~70-80% 减少

### 索引统计（预期）
- **doc_count**: ~8,841,823
- **total_terms**: ~1,000,000 - 2,000,000
- **total_postings**: ~500,000,000 - 1,000,000,000
- **avgdl**: ~50-100（英文文本平均文档长度）

## 下一步

索引构建完成后，可以：
1. 使用 `inspector.exe` 验证索引正确性
2. 开始实现 Phase 3（查询处理器）
3. 测试 BM25 排序算法
4. 评估查询响应时间

## 更多信息

- Phase 1 详细说明：[PHASE1_README.md](PHASE1_README.md)
- Phase 2 详细说明：[PHASE2_README.md](PHASE2_README.md)
- 完整文档：[README.md](README.md)

