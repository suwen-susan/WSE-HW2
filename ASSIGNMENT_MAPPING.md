## 作业要求对照（Assignment → Implementation）

本文件逐条对应课程作业要求与本项目实现，便于检查与答辩。

### 1. 三个独立可执行文件
- 要求：Indexer（解析并写中间 posting）、Merger（生成最终压缩索引）、Querier（交互式检索）。
- 实现：
  - `indexer.exe` → 源码：`src/indexer.cpp`
  - `merger.exe` → 源码：`src/merger.cpp`
  - `querier.exe` → 源码：`src/querier.cpp`（CLI）；可选 Web：`src/web_server.cpp` + `web/`

### 2. 第一二阶段产物写入磁盘，供下一阶段读取
- 要求：所有数据产物写入文件。
- 实现：
  - Phase 1 输出：`output/postings_part_*.tsv`、`output/doc_table.txt`
  - 排序输出：`output/postings_sorted.tsv`
  - Phase 2 输出：`index/postings.docids.bin`、`index/postings.freqs.bin`、`index/lexicon.tsv`、`index/stats.txt`、`index/doc_len.bin`
  - Phase 3 从以上文件加载（见 `src/querier.cpp` / `src/web_server.cpp`）。

### 3. MS MARCO 8.8M passages（或更大）
- 要求：I/O 高效、可在笔记本上运行。
- 实现：
  - Phase 1：流式读取与分片滚动写，避免内存聚合；
  - Phase 2：顺序扫描 + 8MB 读缓冲；块化写二进制；
  - Phase 3：仅按块解压，按需 seek，不加载整个倒排表。

### 4. 索引内容与文件数量（3-5 个文件）
- 要求：倒排表 + 词典 + 页表（docTable）+ 可选块元数据；避免每个 term 一个文件。
- 实现：
  - 倒排表：`postings.docids.bin`（gap+VarByte）、`postings.freqs.bin`（VarByte）；
  - 词典：`lexicon.tsv`（`term df cf docids_offset freqs_offset blocks`）；
  - 统计与文档长度：`stats.txt`、`doc_len.bin`；
  - 页表：`output/doc_table.txt`（Phase 1 产物，Querier 加载）。

### 5. 倒排表格式：块化、二进制、压缩
- 要求：块化、二进制、压缩（推荐 VarByte），docIDs 与 freqs/impact 分离。
- 实现：
  - 块大小：`128`（`src/merger.cpp` 中常量，可调）；
  - 文档 ID：差分写入 + VarByte；
  - 频率：VarByte；
  - 分离存放于 `*.docids.bin` 与 `*.freqs.bin`；
  - 词典记录块数与偏移，支持随机定位与块级读取。

### 6. Query Processor：DAAT + BM25，AND/OR，两种模式
- 要求：给定查询，查词典、seek 定位、DAAT 遍历、返回 top-10 BM25；支持合取/析取；处理一条后继续下一条。
- 实现：
  - 词典加载：`Lexicon::load()`；
  - 倒排访问：`PostingList::open/next/nextGEQ`；
  - BM25：`bm25.hpp`（`idf/score/fullScore`）；
  - 评估器：`QueryEvaluator::evaluateOR/evaluateAND`（`include/querier.hpp`）；
  - Top-K：最小堆（`std::priority_queue` 反向比较）；
  - 交互：CLI REPL（`src/querier.cpp`），或 HTTP `/search`（`src/web_server.cpp`）。

### 7. 不加载所有倒排入内存；可选缓存
- 要求：可部分加载与按需 seek，允许实现缓存（可选）。
- 实现：
  - `PostingList` 块级读取与局部解压；
  - 仅常驻元数据（`lexicon/stats/doc_len/doc_table`）；
  - 可进一步扩展块级跳表或缓存（当前未实现，已在 `PHASE2_README.md` 建议）。

### 8. 压缩方案建议
- 要求：可以使用 VarByte（推荐），或 Simple9/PEF 等；禁止 gzip/bzip2 此类通用压缩。
- 实现：
  - 采用 VarByte（`include/varbyte.hpp`）；
  - docIDs 采用差分；
  - 不使用通用文件压缩工具。

### 9. docID 分配与 termID 使用
- 要求：按解析顺序分配 docID；可不使用 termID。
- 实现：
-  Indexer 解析顺序分配 `internalDocID`，不使用 termID（直接输出 term 文本）。

### 10. CLI 或 Web 前端（加分）
- 要求：CLI 必须；Web 可选加分；可返回 snippets。
- 实现：
  - CLI：`src/querier.cpp`；
  - Web：`src/web_server.cpp` + `web/index.html`；
  - Snippet：`doc_table.txt` 中保存片段，查询结果中展示。

### 11. 文档与可运行性
- 要求：给出使用说明、保证可在本机运行与演示。
- 实现：
  - 快速入门：`QUICKSTART.md`；
  - 阶段说明：`PHASE2_README.md`、`PHASE3_USAGE.md`；
  - 架构与对照：本文件 + `ARCHITECTURE.md`；
  - 示例命令：README/QUICKSTART/PHASE3_USAGE 已提供。

---

## 参考源码清单（映射定位）
- `include/utils.hpp`：分词与归一化
- `include/varbyte.hpp`：VarByte 编解码
- `src/indexer.cpp`：Phase 1（`IndexBuilder::processMSMARCO/parseDocument`）
- `src/merger.cpp`：Phase 2（`process/writeDocIDsBlock/writeFrequenciesBlock/writeStats`）
- `include/index_reader.hpp`：`Lexicon/Stats/DocTable/DocLen/PostingList`
- `include/bm25.hpp`：BM25 公式
- `include/querier.hpp`：`QueryEvaluator::evaluateOR/evaluateAND/processQuery`
- `src/querier.cpp`：CLI 入口
- `src/web_server.cpp`：Web 入口与 `/search` 路由
- `web/index.html`：前端页面


