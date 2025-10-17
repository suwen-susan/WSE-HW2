## 系统架构与分阶段流程

本项目实现了一个典型的三阶段倒排索引与检索系统：

1) Phase 1 Indexer：解析原始集合，输出扁平 posting 流与文档表
2) 全局排序：对所有 posting 行进行 term/docID 的全局排序
3) Phase 2 Merger：将排序后的 posting 归并为块化、二进制、压缩的倒排索引
4) Phase 3 Querier：加载索引元数据，按需块级解压并以 DAAT + BM25 完成检索

### 顶层数据流

```
collection.tsv  --(Indexer)-->  output/postings_part_*.tsv + output/doc_table.txt
         |                                 |
         +---(GNU sort/msort 全局排序)-----+--> output/postings_sorted.tsv
                                            |
                                          (Merger)
                                            |
                                            v
index/postings.docids.bin  (gap+VarByte)
index/postings.freqs.bin   (VarByte)
index/lexicon.tsv          (term → df/cf/offsets/blocks)
index/stats.txt            (doc_count/avgdl/统计)
index/doc_len.bin          (每文档长度，BM25 需要)

                              |
                           (Querier)
                              v
BM25 + DAAT 检索（CLI 或 Web）
```

---

## Phase 1：Indexer（解析与扁平 posting 生成）

- 入口与主要文件：`src/indexer.cpp`
- 文本处理与分词：`include/utils.hpp`

### 输入/输出
- 输入：MS MARCO `collection.tsv`，格式：`originalDocID<TAB>passage`
- 输出：
  - `output/doc_table.txt`：`internalDocID<TAB>originalDocID<TAB>snippet`
  - `output/postings_part_*.tsv`：`term<TAB>docID<TAB>tf`（滚动分片写出）

### 核心流程
- 逐行读取 TSV，解析出 `docName` 与 `content`；
- 用 `tokenize_words`（先 `normalize`）进行分词，保留所有词（数字、单字符、停用词）；
- 统计当前文档内 `term -> tf`，写出扁平 posting 行；
- 生成并写出 `doc_table.txt`（包含 snippet）；
- 依据分片字节阈值滚动创建新 `postings_part_N.tsv`，控制单文件大小。

### 实现锚点
- 分词：`include/utils.hpp` 中 `normalize()` 与 `tokenize_words()`
- 索引器：`src/indexer.cpp` 中 `IndexBuilder::parseDocument`、`processMSMARCO`

### 复杂度与效率
- 单次线性扫描 O(total terms)；
- 流式写出、无需聚合；
- 分片阈值避免单文件过大，利于后续排序与 I/O。

---

## 全局排序（msort / GNU sort）

目的：将多个分片的 posting 行统一为按 term（字典序）主键、docID（数值升序）次键的全局排序文件，便于 Phase 2 线性归并。

示例命令请参见 `QUICKSTART.md` 与 `README.md`，输出 `output/postings_sorted.tsv`。

---

## Phase 2：Merger（归并为块化压缩倒排索引）

- 入口与主要文件：`src/merger.cpp`
- 编码工具：`include/varbyte.hpp`

### 输入/输出
- 输入：`output/postings_sorted.tsv`
- 输出：
  - `index/postings.docids.bin`：docID 块（差分 + VarByte）
  - `index/postings.freqs.bin`：tf 块（VarByte）
  - `index/lexicon.tsv`：`term df cf docids_offset freqs_offset blocks`
  - `index/stats.txt`：统计（`doc_count/avgdl/total_terms/total_postings` 等）
  - `index/doc_len.bin`：每文档长度（BM25 需要）

### 核心流程
1) 采用 8MB 读缓冲顺序读取 `postings_sorted.tsv`；
2) 按 `term` 分组聚合当前倒排表；
3) 以块大小 128（可调）切分：
   - docIDs：首个 docID 视作与 0 的 gap，后续写差分；`varbyte::encode` 压缩；
   - freqs：直接 `varbyte::encode`；
   - 同步累计 `cf`，并更新 `docLengths[docID] += tf`；
4) 每个 term 结束后在 `lexicon.tsv` 写入 `df/cf` 与两个偏移、块数；
5) 末尾写出 `doc_len.bin` 与 `stats.txt`（含 `avgdl`）。

### 实现锚点
- 主流程：`IndexMerger::process()`（流式读取、分组、写出）
- 写块：`writeDocIDsBlock()`（差分+VarByte）、`writeFrequenciesBlock()`（VarByte + 更新 `docLengths`）
- 统计输出：`writeStats()`（写 `doc_len.bin` 与 `stats.txt`）

### 格式要点
- 分离文件存储 docIDs 与 freqs；
- 每块写入 `block_len` 头部；
- 词典记录偏移与块数，便于随机定位与块级读取。

---

## Phase 3：Querier（BM25 + DAAT 查询处理）

- CLI：`src/querier.cpp`（交互式命令行）
- Web：`src/web_server.cpp`（简易多线程 HTTP 服务），前端：`web/index.html`
- 索引读取与 API：`include/index_reader.hpp`
- BM25：`include/bm25.hpp`
- 查询评估器：`include/querier.hpp`

### 启动与加载
- 加载 `lexicon.tsv` → 内存映射为 `term -> TermMeta`；
- 加载 `stats.txt`（`doc_count/avgdl`）；
- 加载 `doc_len.bin` 到数组（按 `docID` 下标访问长度）；
- 加载 `doc_table.txt`（`originalID/doc snippet` 用于展示）。

### 倒排表 API（PostingList）
- `open(meta, docidsFile, freqsFile)`：seek 到 `docids_offset/freqs_offset`，读取第一块；
- `next()`：在当前块推进指针，必要时读取下一块；
- `nextGEQ(target)`：向前跳至 `>= target` 的第一个文档；
- `doc()/freq()/valid()`：获取当前文档与频率及有效性。

### DAAT 遍历与 BM25
- OR（析取）：在所有列表上取最小 docID，累积匹配项的 BM25 分值；
- AND（合取）：维护最大 docID，将其他列表推进到该 docID，全部命中时计算分值；
- Top-K：用最小堆维持前 K 个结果；
- BM25：`idf(N, df)` 与 `score(tf, dl, avgdl, k1, b)`，最终 `idf * score`。

### CLI 与 Web 路径
- CLI：`querier.exe index output/doc_table.txt --mode=or --k=10 --k1=0.9 --b=0.4` → REPL（支持 `/and` `/or`）
- Web：`web_server.exe index output/doc_table.txt 8080` → `GET /search?q=...&mode=or|and&k=10&k1=0.9&b=0.4`

---

## 关键数据格式

### 1) postings TSV（中间文件）
`term<TAB>docID<TAB>tf`，全局排序按 `(term asc, docID asc)`。

### 2) lexicon.tsv
`term\tdf\tcf\tdocids_offset\tfreqs_offset\tblocks_count`

### 3) postings.docids.bin / postings.freqs.bin
- 每块格式：`[block_len: VarByte] [values: VarByte ...]`
- docIDs 使用 gap 编码：`docID_0` 视作 gap 自 0，后续写与前一个的差。

### 4) stats.txt
`doc_count`、`avgdl`、`total_terms`、`total_postings`、`total_doc_length`。

### 5) doc_len.bin
按 `docID` 顺序写入 `uint32_t` 文档长度。

---

## 性能与内存要点
- Phase 1：流式处理、分片输出，避免全量 aggregate；
- 排序：使用系统排序工具（多线程/外排序能力）；
- Phase 2：一次线性扫描 O(N)；块化 + VarByte 减少 I/O；
- Phase 3：不加载倒排表，仅块级按需解压；`nextGEQ` 前向跳转；`avgdl/doc_len` 常驻内存。

---

## 源码索引（便于快速定位）
- 分词：`include/utils.hpp`
- VarByte：`include/varbyte.hpp`
- Indexer：`src/indexer.cpp`（`IndexBuilder::processMSMARCO/parseDocument`）
- Merger：`src/merger.cpp`（`process/writeDocIDsBlock/writeFrequenciesBlock/writeStats`）
- 索引读取：`include/index_reader.hpp`（`Lexicon/Stats/DocTable/DocLen/PostingList`）
- BM25：`include/bm25.hpp`
- 查询评估：`include/querier.hpp`（`evaluateOR/evaluateAND/processQuery`）
- CLI：`src/querier.cpp`
- Web：`src/web_server.cpp` 与 `web/index.html`


