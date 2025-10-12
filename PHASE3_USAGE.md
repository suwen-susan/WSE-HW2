# Phase 3: 查询处理器使用指南

## 前置条件

在使用 querier.exe 之前，需要：

1. ✅ 完成 Phase 1（生成 posting 流）
2. ✅ 完成 msort 全局排序
3. ✅ 完成 Phase 2（生成压缩索引 + doc_len.bin）

确保以下文件存在：
```
index/
  ├── postings.docids.bin    # 压缩的 docID 序列
  ├── postings.freqs.bin     # 压缩的 frequency 序列
  ├── lexicon.tsv            # 词典
  ├── stats.txt              # 统计信息（doc_count, avgdl）
  └── doc_len.bin            # 文档长度（BM25 需要）

output/
  └── doc_table.txt          # 文档 ID 到文档名的映射
```

## 基本用法

### 1. 启动查询器

```bash
querier.exe <index_dir> <doc_table_path> [options]
```

**参数说明**：
- `<index_dir>`: 索引目录（包含 lexicon.tsv, stats.txt 等）
- `<doc_table_path>`: 文档表路径（通常是 output/doc_table.txt）
- `[options]`: 可选参数

**示例**：
```bash
# 使用默认参数（OR 模式，Top-10，k1=0.9, b=0.4）
querier.exe index output/doc_table.txt

# 使用 AND 模式
querier.exe index output/doc_table.txt --mode=and

# 返回 Top-20 结果
querier.exe index output/doc_table.txt --k=20

# 自定义 BM25 参数
querier.exe index output/doc_table.txt --k1=1.2 --b=0.75
```

### 2. 可选参数

| 参数 | 说明 | 默认值 |
|-----|------|--------|
| `--mode=or\|and` | 查询模式（析取/合取） | `or` |
| `--k=N` | 返回结果数量 | `10` |
| `--k1=X` | BM25 k1 参数（词频饱和） | `0.9` |
| `--b=X` | BM25 b 参数（长度归一化） | `0.4` |

**BM25 参数调优建议**：
- `k1 ∈ [0.8, 1.2]`: 较大值更重视高频词
- `b ∈ [0.3, 0.7]`: 较大值更重视长度惩罚

## 交互式查询

### 基本操作

启动后，会进入交互式 REPL（Read-Eval-Print Loop）：

```
> 输入查询字符串
  显示 Top-K 结果
> 输入下一个查询
  ...
> /quit
  退出
```

### 查询示例

```bash
# 启动
querier.exe index output/doc_table.txt

# 进入交互模式
> machine learning
Query terms: machine, learning (OR mode)

Top 10 results (in 25 ms):
--------------------------------------------------------------------------------
 Rank       DocID       Score  Document
--------------------------------------------------------------------------------
    1      1234567      12.3456  machine learning algorithms and applications
    2      2345678      11.2345  introduction to machine learning
    ...

> /and machine learning artificial intelligence
Query terms: machine, learning, artificial, intelligence (AND mode)

Top 5 results (in 15 ms):
--------------------------------------------------------------------------------
 Rank       DocID       Score  Document
--------------------------------------------------------------------------------
    1      3456789      15.6789  machine learning and artificial intelligence
    ...

> /quit
Goodbye!
```

### 交互命令

| 命令 | 说明 | 示例 |
|-----|------|------|
| `<query>` | 使用默认模式查询 | `python programming` |
| `/or <query>` | 临时切换到 OR 模式 | `/or computer science` |
| `/and <query>` | 临时切换到 AND 模式 | `/and deep learning neural network` |
| `/quit` | 退出程序 | `/quit` |
| `/exit` | 退出程序（同 /quit） | `/exit` |

## 查询模式详解

### OR 模式（析取）

- **语义**：返回包含**至少一个**查询词的文档
- **适用**：一般性搜索，召回率优先
- **示例**：
  ```
  > /or python java programming
  # 返回包含 "python" 或 "java" 或 "programming" 的文档
  ```

### AND 模式（合取）

- **语义**：返回包含**所有**查询词的文档
- **适用**：精确搜索，准确率优先
- **示例**：
  ```
  > /and machine learning algorithm
  # 只返回同时包含所有三个词的文档
  ```

## 输出格式

```
Query terms: <term1>, <term2>, ... (<MODE> mode)

Top N results (in X ms):
--------------------------------------------------------------------------------
 Rank       DocID       Score  Document
--------------------------------------------------------------------------------
    1      1234567      12.3456  document name or URL
    2      2345678      11.2345  document name or URL
    ...
```

**字段说明**：
- **Rank**: 排名（1 = 最相关）
- **DocID**: 文档 ID
- **Score**: BM25 得分（越高越相关）
- **Document**: 文档名称或 URL

## 完整流程示例

### 场景 1: 小测试集

```bash
# Step 1: 生成测试数据
echo 1	The quick brown fox jumps over the lazy dog > test.tsv
echo 2	Python programming language for machine learning >> test.tsv
echo 3	Java programming and software development >> test.tsv

# Step 2: 索引
indexer.exe test.tsv test_out 1

# Step 3: 排序
sort test_out/postings_part_0.tsv > test_sorted.tsv

# Step 4: 合并
merger.exe test_sorted.tsv test_idx

# Step 5: 查询
querier.exe test_idx test_out/doc_table.txt

> programming
# 返回文档 2 和 3

> /and python programming
# 只返回文档 2

> /quit
```

### 场景 2: 完整数据集（8.8M 文档）

```bash
# 假设已完成 Phase 1 和 Phase 2

# 启动查询器
querier.exe index output/doc_table.txt --mode=or --k=10

# 示例查询
> coronavirus vaccine
> machine learning applications
> natural language processing
> /and deep learning neural network
> /quit
```

## 一键脚本（Windows）

仓库根目录提供 `run_all.bat`，可一键完成：编译四个程序（indexer/merger/inspector/querier）→ 索引构建 → 全局排序（自动检测 msort，若不可用则回退 PowerShell 或 GNU sort）→ 合并 → 启动 `querier.exe`。

```bat
run_all.bat
```

脚本启动后会给出示例交互，亦支持从文件批量读取查询：

```bat
querier.exe index output\doc_table.txt < test_queries_batch.txt > results.txt
```

## 常见问题

### Q1: 启动时报错 "Cannot open lexicon"
**原因**：索引目录路径错误或文件缺失  
**解决**：
```bash
# 检查文件是否存在
dir index\lexicon.tsv
dir index\stats.txt
dir index\doc_len.bin

# 如果缺少 doc_len.bin，重新运行 Phase 2
merger.exe output/postings_sorted.tsv index
```

### Q2: 查询返回空结果
**原因**：
1. 查询词在索引中不存在（如拼写错误）
2. AND 模式下没有文档同时包含所有词

**解决**：
- 检查分词是否一致（与 Phase 1 相同规则）
- 尝试 OR 模式增加召回
- 使用 inspector 检查词是否在索引中：
  ```bash
  inspector.exe index <term>
  ```

### Q3: 程序运行缓慢
**原因**：
1. 索引文件在 HDD 上（而非 SSD）
2. 查询词过于常见（如停用词 "the"）

**解决**：
- 将索引文件移到 SSD
- BM25 会自动降低常见词的权重（IDF）
- 首次查询会慢（磁盘缓存），后续会快

### Q4: 如何查看某个词的倒排表？
**使用** `inspector.exe`：
```bash
inspector.exe index python
```

### Q5: 如何批量测试查询？
创建查询文件 `queries.txt`：
```
machine learning
deep learning
python programming
```

使用重定向：
```bash
querier.exe index output/doc_table.txt < queries.txt > results.txt
```

## 性能数据（预期）

### 8.8M 文档数据集

| 查询类型 | 响应时间 | 说明 |
|---------|---------|------|
| 单词查询（常见词） | 10-50 ms | 如 "learning" |
| 单词查询（罕见词） | 1-5 ms | 如 "tensorflow" |
| 多词 OR 查询（2-3词） | 20-100 ms | 如 "machine learning" |
| 多词 AND 查询（2-3词） | 10-50 ms | AND 通常更快（早停） |
| 复杂查询（5+词） | 50-200 ms | 取决于词频 |

**影响因素**：
- 磁盘速度（SSD vs HDD）
- 查询词的 document frequency
- Top-K 大小（K=10 vs K=100）
- 操作系统文件缓存

## 高级用法

### 1. 自定义 BM25 参数

```bash
# 更重视词频（适合短文本）
querier.exe index output/doc_table.txt --k1=1.5 --b=0.3

# 更重视长度惩罚（适合长文本）
querier.exe index output/doc_table.txt --k1=0.8 --b=0.8
```

### 2. 批量查询评估

创建评估脚本 `eval_queries.sh`：
```bash
#!/bin/bash
echo "machine learning" | querier.exe index output/doc_table.txt --k=100
echo "deep neural networks" | querier.exe index output/doc_table.txt --k=100
# ... 更多查询
```

### 3. 性能测试

测试查询响应时间：
```bash
time echo "machine learning" | querier.exe index output/doc_table.txt
```

## 代码架构

```
querier.exe
├── Lexicon          加载词典（term -> metadata）
├── Stats            加载统计信息（N, avgdl）
├── DocLen           加载文档长度（BM25 需要）
├── DocTable         加载文档表（docID -> name）
├── QueryEvaluator   查询评估器
│   ├── evaluateOR() DAAT 析取查询
│   └── evaluateAND()DAAT 合取查询
├── PostingList      倒排表遍历器（块级解压）
│   ├── next()       推进到下一个文档
│   └── nextGEQ()    推进到 >= target 的文档
└── BM25             排序算法
    ├── idf()        计算 IDF
    └── score()      计算 BM25 得分
```

## 下一步

查询器就绪后，可以：
1. ✅ 测试各种查询模式和参数
2. ✅ 评估检索质量和性能
3. ⏳ （可选）添加跳表优化
4. ⏳ （可选）添加查询缓存
5. ⏳ （可选）实现 Web 界面
6. ✅ 准备课程 demo 和报告

## 参考资料

- BM25 算法：Robertson & Zaragoza (2009)
- DAAT 遍历：课程讲义 Chapter 5
- VarByte 编码：课程讲义 Chapter 6

