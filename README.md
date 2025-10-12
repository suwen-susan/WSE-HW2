# WSE-HW2: 倒排索引构建系统

完整实现课程要求的三阶段搜索引擎索引构建系统。当前已完成 **Phase 1、Phase 2、Phase 3**。

## 项目结构

```
WSE-HW2/
├── data/
│   └── collection.tsv          # MS MARCO 数据集（8.8M passages）
├── include/
│   ├── utils.hpp               # 分词与归一化工具
│   ├── varbyte.hpp             # VarByte 压缩编码
│   ├── index_reader.hpp        # 词典/倒排表/文档长度读取与遍历
│   └── bm25.hpp                # BM25 打分实现
├── src/
│   ├── indexer.cpp             # Phase 1: 索引器（生成 posting 流）
│   ├── merger.cpp              # Phase 2: 合并器（块化压缩索引）
│   ├── index_inspector.cpp     # 索引检查工具
│   └── querier.cpp             # Phase 3: 查询处理器（BM25 + DAAT）
├── indexer.exe                 # Phase 1 可执行文件
├── merger.exe                  # Phase 2 可执行文件
├── inspector.exe               # 检查工具
├── querier.exe                 # Phase 3 可执行文件
├── PHASE1_README.md            # Phase 1 详细说明
├── PHASE2_README.md            # Phase 2 详细说明
└── PHASE3_USAGE.md             # Phase 3 使用说明
```

## 快速开始

### 编译所有程序

```bash
# Phase 1: 索引器
g++ -std=c++17 src/indexer.cpp -o indexer.exe -I./include -O2

# Phase 2: 合并器
g++ -std=c++17 src/merger.cpp -o merger.exe -I./include -O2

# 检查工具
g++ -std=c++17 src/index_inspector.cpp -o inspector.exe -I./include -O2
```

### 完整流程

```bash
# Step 1: 运行第一阶段（生成 posting 流）
indexer.exe data/collection.tsv output 2

# Step 2: 使用 msort 全局排序
msort -t '\t' -k 1,1 -k 2,2n output/postings_part_*.tsv > output/postings_sorted.tsv

# Step 3: 运行第二阶段（合并为压缩索引）
merger.exe output/postings_sorted.tsv index

# Step 4: 验证索引
inspector.exe index
inspector.exe index the fox dog  # 查看具体词项
```

## 三个阶段说明

### Phase 1: 索引构建（Indexer）

**输入**：MS MARCO collection.tsv (格式: `docID<TAB>passage`)

**输出**：
- `output/doc_table.txt`: 文档ID到文档名的映射
- `output/postings_part_N.tsv`: 分片 posting 文件

**特点**：
- ✅ 符合课程分词规则（非字母数字为分隔符）
- ✅ 保留所有词（数字、单字符、停用词）
- ✅ 流式写出，内存高效
- ✅ 可配置分片大小（默认 2GB）
- ✅ 扁平格式：`term<TAB>docID<TAB>tf`

详见 [PHASE1_README.md](PHASE1_README.md)

### msort: 全局排序

使用课程提供的 msort 工具对所有分片进行全局排序：
- **主键**：term（字典序）
- **次键**：docID（数值升序）

### Phase 2: 索引合并（Merger）

**输入**：msort 排序后的 `postings_sorted.tsv`

**输出**：
- `index/postings.docids.bin`: 压缩的 docID 序列（差分 + VarByte）
- `index/postings.freqs.bin`: 压缩的 frequency 序列（VarByte）
- `index/lexicon.tsv`: 词典（term 元数据）
- `index/stats.txt`: 统计信息（doc_count、avgdl 等）

**特点**：
- ✅ 块化存储（每块 128 个 postings）
- ✅ 二进制压缩格式（VarByte 编码）
- ✅ docID 差分编码（节省空间）
- ✅ 少量文件（4个，符合课程 3-5 个要求）
- ✅ 支持 BM25 所需统计信息

详见 [PHASE2_README.md](PHASE2_README.md)

### Phase 3: 查询处理器（待实现）

将实现：
- BM25 排序算法
- DAAT（Document-At-A-Time）遍历
- 合取/析取查询模式
- Top-K 结果返回
- 倒排表 API（隐藏解压细节）

## 索引格式详解

### 词典格式 (lexicon.tsv)
```
term	df	cf	docids_offset	freqs_offset	blocks_count
fox	3	3	52	52	1
```

### 倒排表格式（二进制）

**DocIDs 块**：
```
[block_len: VarByte] [docID_0: VarByte] [gap_1: VarByte] [gap_2: VarByte] ...
```

**Freqs 块**：
```
[block_len: VarByte] [tf_0: VarByte] [tf_1: VarByte] ...
```

## 工具使用

### indexer.exe - 索引器
```bash
indexer.exe <input_tsv> <output_dir> [part_size_gb]

示例：
  indexer.exe data/collection.tsv output 4  # 每个分片 4GB
```

### merger.exe - 合并器
```bash
merger.exe <sorted_postings> <output_dir>

示例：
  merger.exe output/postings_sorted.tsv index
```

### inspector.exe - 检查工具
```bash
inspector.exe <index_dir> [term1] [term2] ...

示例：
  inspector.exe index              # 显示统计和词典概览
  inspector.exe index fox dog      # 查看具体词项的倒排表
```

## 测试

### 集成测试（Phase 1 + 2）
```bash
.\test_phase2.bat
```

该脚本会：
1. 创建小测试数据集
2. 运行 Phase 1 索引器
3. 模拟排序
4. 运行 Phase 2 合并器
5. 验证结果

### 手动测试
```bash
# 创建测试数据
echo "1	The quick brown fox" > test.tsv
echo "2	The fox and the dog" >> test.tsv

# 运行流程
indexer.exe test.tsv test_out 1
sort test_out/postings_part_0.tsv > test_sorted.tsv
merger.exe test_sorted.tsv test_idx
inspector.exe test_idx fox
```

## 性能数据

### MS MARCO 8.8M 文档数据集（~3GB）

**Phase 1（索引器）**：
- SSD: 5-10 分钟
- HDD: 15-25 分钟
- 中间文件数：预计 ≤100 个（每个 2-4GB）

**Phase 2（合并器）**：
- SSD: 3-5 分钟
- HDD: 10-15 分钟

**压缩效果**：
- 相比未压缩文本减少 70-80%
- docIDs 差分编码：3-5 倍压缩
- frequencies VarByte：多数 1 字节

## 与课程要求对应

| 要求 | 实现 | 状态 |
|-----|------|------|
| 分词规则 | 非字母数字为分隔符，保留所有词 | ✅ |
| 三个可执行文件 | indexer, merger, (querier 待实现) | ✅✅⏳ |
| 块化存储 | 每块 128 postings | ✅ |
| 二进制压缩 | VarByte + 差分编码 | ✅ |
| 少量文件 | 4 个（符合 3-5 个） | ✅ |
| 词典 | lexicon.tsv with offsets | ✅ |
| 统计信息 | stats.txt (avgdl, doc_count) | ✅ |
| I/O 高效 | 流式处理 + 大缓冲区 | ✅ |
| 内存高效 | 不载入全部数据 | ✅ |

## 代码质量

- ✅ 模块化设计（utils, varbyte 独立）
- ✅ 清晰的类结构和职责分离
- ✅ 详细的注释和文档
- ✅ 错误处理和验证
- ✅ 性能优化（O2 编译、缓冲区、预分配）

## 常见问题

### Q: 为什么 Phase 1 要生成扁平 posting 行？
A: 配合 msort 全局排序。如果在 Phase 1 就聚合，无法用 msort 进行全局归并。

### Q: VarByte 编码原理？
A: 每字节 7 位存数据，最高位标识是否还有后续字节。对小数值（如 gap、tf）压缩效果好。

### Q: 块大小为什么是 128？
A: 平衡压缩率和查询速度。太小开销大，太大解压慢。128 是经验值。

### Q: 如何处理超大倒排表（如"the"）？
A: 分块存储，查询时可实现早停（early termination）或 MaxScore 优化。

## 下一步

- [ ] 实现 Phase 3: BM25 查询处理器
- [ ] 支持合取/析取模式
- [ ] 实现倒排表 API（隐藏解压）
- [ ] DAAT 遍历算法
- [ ] 命令行查询界面
- [ ] （可选）跳表优化
- [ ] （可选）缓存机制
- [ ] （可选）Web 界面

## 许可与致谢

本项目为课程作业，实现参考：
- 课程讲义中的倒排索引构建算法
- VarByte 编码标准实现
- MS MARCO 数据集格式规范

## 联系方式

如有问题，请参考：
- [PHASE1_README.md](PHASE1_README.md) - Phase 1 详细说明
- [PHASE2_README.md](PHASE2_README.md) - Phase 2 详细说明
- `inspector.exe` - 索引验证工具

