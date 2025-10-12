# 第二阶段实现说明

## 概述

第二阶段将第一阶段产出的、经 msort 全局排序后的 posting 流，聚合为**块化、二进制、压缩**的最终倒排索引。

## 核心文件

### 1. `include/varbyte.hpp`
**VarByte 变长整数编码工具**

- **编码原理**：每个字节用 7 位存数据，最高位标识是否还有后续字节
- **优势**：对小数值高效压缩（如 docID gap、tf 通常都较小）
- **函数**：
  - `encode(ostream, value)`: 编码单个整数到输出流
  - `encode_batch(buffer, values)`: 批量编码
  - `decode(istream)`: 解码单个整数（第三阶段查询时使用）
  - `decode_from_buffer(ptr)`: 从内存缓冲区解码

**示例**：
```cpp
uint32_t gap = 15;
varbyte::encode(outFile, gap);  // 写入 1 字节：0x0F

uint32_t largeGap = 300;
varbyte::encode(outFile, largeGap);  // 写入 2 字节：0xAC 0x02
```

### 2. `src/merger.cpp`
**倒排索引合并器主程序**

#### 类设计：`IndexMerger`

**核心参数**：
- `BLOCK_SIZE = 128`：每块 posting 数量（可调整）
- `READ_BUFFER_SIZE = 8MB`：输入文件读缓冲

**处理流程**：
1. **流式读取** `postings_sorted.tsv`（逐行，不全部载入内存）
2. **按 term 分组**：检测 term 切换，累积当前 term 的所有 postings
3. **块化写入**：
   - 每 128 个 posting 一块
   - docIDs：差分编码 + VarByte
   - frequencies：VarByte
4. **生成词典**：记录每个 term 的元数据
5. **统计信息**：计算 doc_count、avgdl（BM25 需要）

## 输出文件格式

### 1. `postings.docids.bin`（二进制）
**块化存储所有 term 的 docID 序列**

```
每个块格式：
  block_length: VarByte (本块 posting 数量)
  docID_0: VarByte (第一个 docID，视作与 0 的 gap)
  gap_1: VarByte (docID_1 - docID_0)
  gap_2: VarByte (docID_2 - docID_1)
  ...
```

**示例**：term "fox" 的 postings 是 [(1,2), (6,1), (9,1)]
```
块长度：3
docID_0：1      (gap from 0)
gap_1：5        (6 - 1)
gap_2：3        (9 - 6)
```

### 2. `postings.freqs.bin`（二进制）
**块化存储所有 term 的 frequency 序列**

```
每个块格式：
  block_length: VarByte
  tf_0: VarByte
  tf_1: VarByte
  ...
```

**与 docids 对齐**：第 i 个 frequency 对应第 i 个 docID

### 3. `lexicon.tsv`（文本，便于调试）
**词典：每个 term 的元数据**

```
格式：term<TAB>df<TAB>cf<TAB>docids_offset<TAB>freqs_offset<TAB>blocks_count

字段说明：
- term: 词项
- df: document frequency (包含该词的文档数)
- cf: collection frequency (该词在整个集合中出现的总次数，即所有 tf 之和)
- docids_offset: 该 term 在 postings.docids.bin 中的起始字节偏移
- freqs_offset: 该 term 在 postings.freqs.bin 中的起始字节偏移
- blocks_count: 该 term 的倒排表分了几块
```

**示例**：
```
fox	3	3	52	52	1
```
表示：
- "fox" 出现在 3 个文档中（df=3）
- 总共出现 3 次（cf=3）
- docIDs 数据从 postings.docids.bin 的第 52 字节开始
- frequencies 数据从 postings.freqs.bin 的第 52 字节开始
- 只有 1 个块（因为 posting 数 < 128）

### 4. `stats.txt`（文本）
**索引统计信息（BM25 查询需要）**

```
doc_count: 文档总数
total_terms: 唯一词项总数
total_postings: posting 总数
avgdl: 平均文档长度
total_doc_length: 所有文档长度之和
```

## 使用方法

### 完整流程（Phase 1 → msort → Phase 2）

```bash
# Step 1: 运行第一阶段索引器
indexer.exe data/collection.tsv output 2

# Step 2: 使用 msort 全局排序（课程提供的工具）
# Linux/macOS:
msort -t '\t' -k 1,1 -k 2,2n output/postings_part_*.tsv > output/postings_sorted.tsv

# Windows (如果 msort 不支持通配符，需要显式列出所有文件):
msort -t '\t' -k 1,1 -k 2,2n output/postings_part_0.tsv output/postings_part_1.tsv ... > output/postings_sorted.tsv

# Step 3: 运行第二阶段合并器
merger.exe output/postings_sorted.tsv index

# 结果：
# index/postings.docids.bin
# index/postings.freqs.bin
# index/lexicon.tsv
# index/stats.txt
```

### 编译

```bash
# 编译 merger
g++ -std=c++17 src/merger.cpp -o merger.exe -I./include -O2

# -O2: 启用优化（重要，显著提升性能）
```

### 测试

```bash
# 运行集成测试（包含 Phase 1 + Phase 2）
.\test_phase2.bat

# 或手动测试小数据集
indexer.exe test_data.tsv test_output 1
sort test_output/postings_part_0.tsv > test_sorted.tsv
merger.exe test_sorted.tsv test_index
```

## 代码结构与可读性设计

### 1. 模块化设计
- **varbyte.hpp**：独立的编码模块，可复用
- **merger.cpp**：单一职责，只负责索引合并

### 2. 清晰的类结构
```cpp
class IndexMerger {
private:
    // 配置参数（常量）
    static constexpr size_t BLOCK_SIZE = 128;
    
    // 输入输出（明确分离）
    std::string inputFile;
    std::ofstream docIdsFile, freqsFile, lexiconFile;
    
    // 统计信息（集中管理）
    uint64_t totalTerms, totalPostings, docCount;
    
public:
    // 主流程
    void process();
    
private:
    // 子功能（私有方法，明确职责）
    void writeInvertedList(...);
    void writeDocIDsBlock(...);
    void writeFrequenciesBlock(...);
    void writeStats();
};
```

### 3. 注释与文档
- 每个函数都有清晰的功能说明
- 关键算法有注释解释（如差分编码）
- 文件格式有详细文档说明

### 4. 错误处理
- 文件打开失败检查
- 格式错误行的警告（而非崩溃）
- 清晰的错误消息

### 5. 性能优化
- 读缓冲区（8MB）减少系统调用
- 预分配容器容量（`reserve`）
- VarByte 编码节省磁盘空间
- 流式处理，避免全部载入内存

## 性能特点

### 时间复杂度
- **O(N)**：N 为 posting 总数
- 单次线性扫描，无需额外排序

### 空间复杂度
- **O(M)**：M 为单个 term 的最大 posting 数
- 流式处理，不需要全部载入内存
- docLengths 数组：O(D)，D 为文档数（对于 8.8M 文档约 35MB）

### 压缩效果
- **docIDs**：差分 + VarByte，通常压缩 3-5 倍
- **frequencies**：VarByte，多数 tf < 10，每个仅需 1 字节
- **总体**：相比未压缩文本，通常减少 70-80% 空间

### 估算运行时间（基于 8.8M 文档数据集）
- **SSD**：3-5 分钟
- **HDD**：10-15 分钟

主要瓶颈：
1. 读取排序后的文件（I/O）
2. VarByte 编码（CPU，但很快）
3. 写入二进制文件（I/O）

## 与课程要求的对应

| 课程要求 | 实现方式 |
|---------|---------|
| 块化存储 | 每 128 个 posting 一块 |
| 二进制格式 | `.bin` 文件，VarByte 编码 |
| 压缩 | docID 差分 + VarByte |
| 少量文件 | 4 个文件（符合 3-5 个要求）|
| 词典 | lexicon.tsv 记录 offset 和 df/cf |
| 文档表 | 沿用第一阶段的 doc_table.txt |
| 统计信息 | stats.txt 包含 avgdl（BM25 需要）|

## 后续：第三阶段（查询处理器）

第二阶段产出的索引已可直接用于查询：

1. **加载词典**到内存（哈希表或排序数组，支持二分查找）
2. **读取 stats.txt**获取 avgdl、doc_count
3. **查询时**：
   - 根据词典找到 docids_offset、freqs_offset
   - `seek` 到对应位置
   - 按块解压 docIDs 和 frequencies
   - 实现 DAAT（Document-At-A-Time）遍历
   - BM25 打分，返回 top-K

VarByte 解码已在 `varbyte.hpp` 中提供，可直接使用。建议参考 `PHASE3_USAGE.md` 获取 `querier.exe` 的编译与交互用法。

### 一键脚本入口（Windows）

已提供 `run_all.bat`，可在仓库根目录直接运行以完成编译、索引构建、排序合并与查询启动：

```bat
run_all.bat
```

脚本会自动侦测 `msort` 可用性，不可用时回退到 PowerShell 或 GNU sort。

## 已知限制与改进方向

### 当前实现
- ✅ 满足课程所有基本要求
- ✅ 代码结构清晰、可读性好
- ✅ 性能高效、空间压缩

### 可选改进（额外分或性能提升）
1. **跳表（Skip Lists）**：
   - 在词典中额外存储每块的最大 docID
   - 查询时可跳过不相关的块
   
2. **Impact-based 索引**（可选替代 tf）：
   - 存储量化的 impact score（8-bit 定长）
   - 支持 MaxScore、WAND 等优化算法
   
3. **二进制词典**：
   - 当前 lexicon.tsv 是文本（便于调试）
   - 可生成二进制版本，加载更快
   
4. **分段索引**：
   - 高频词单独存储
   - 低频词合并存储
   
5. **多线程**：
   - 块编码可并行
   - 需要更复杂的同步机制

## 测试与验证

### 自动测试
```bash
.\test_phase2.bat
```

### 手动验证
```bash
# 检查词典格式
head -20 index/lexicon.tsv

# 检查统计信息
cat index/stats.txt

# 检查二进制文件大小
ls -lh index/*.bin

# 验证压缩率
# 比较 postings_sorted.tsv 与 *.bin 的大小
```

### 正确性检查
- df（文档频率）应合理
- cf（集合频率）>= df
- avgdl 应在合理范围（英文通常 50-200）
- 文件大小应显著小于未压缩文本

## 常见问题

### Q1: msort 在 Windows 下如何使用？
A: 课程应提供 msort.exe，使用方式类似 Linux。如果不支持通配符，需显式列出所有输入文件。

### Q2: 为什么要分 docids 和 freqs 两个文件？
A: 课程建议的设计，优势：
- 查询时可选择性加载（只需 docIDs 或两者都要）
- 支持不同的压缩策略（docIDs 用差分，freqs 直接 VarByte）

### Q3: 块大小为什么选 128？
A: 平衡因素：
- 太小：块元数据开销大
- 太大：解压整块的成本高，跳表效果差
- 128 是经验值，可根据数据特征调整（64-256 都合理）

### Q4: 如何处理超大倒排表（如停用词"the"）？
A: 当前实现：
- 按块写入，单个 term 占多个块
- 第三阶段可实现早停（early termination）
- 可选：对超高频词单独优化或截断

### Q5: 压缩后还能快速查询吗？
A: 可以！
- VarByte 解码非常快（~1-2ns/整数，现代 CPU）
- 块化设计支持按块解压，无需解压整个列表
- DAAT 遍历时边解压边处理

## 总结

第二阶段实现了一个高效、符合课程要求的索引合并器：
- ✅ 块化、二进制、压缩存储
- ✅ 少量文件（4个）
- ✅ 支持 BM25 所需的统计信息
- ✅ 代码结构清晰、注释完善
- ✅ 性能优化到位

可直接用于第三阶段的查询处理器开发。

