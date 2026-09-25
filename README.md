# C++ Web Crawler & Downloader

長庚大學資訊工程學系「計算機網路」期末專題。

本專題以 C++ 實作一套可供網站離線瀏覽的 Web Crawler / Downloader。依課程要求，不使用現成 HTTP 函式庫，而是透過 WinSock 建立網路連線、自行產生 HTTP GET Request 並解析 HTTP Response，再進一步完成網頁遞迴、URL 處理與多執行緒檔案下載。

**Course:** 計算機網路  
**Institution:** 長庚大學 資訊工程學系  
**Instructor:** 李春良教授  
**Language:** C++  
**Networking:** WinSock / HTTP  
**Project Type:** Individual Term Project

---

## Repository Structure

```text
CPP-Web-Crawler-Downloader/
│
├── B1229062-VS_code(下載檔案)/
│   ├── 0/資料夾裡面放的是爬蟲教授指定的網頁所下載下來的內容
│   ├── main.cpp
│   ├── downloader.cpp
│   ├── downloader.h
│   ├── advanced.cpp
│   ├── advanced.h
│   ├── util.h
│   └── web.exe
│
├── Term_Project_Report.pdf
│ 
└── README.md
```

`B1229062-VS_code(下載檔案)/` 保存專題原始程式碼；`Term_Project_Report.pdf` 則收錄期末專題報告。

---

## Project Overview

專題目標為實作一套能從指定 URL 開始下載網頁內容的工具。

除了基本的 HTTP 網頁下載外，程式亦需解析 HTML 中的連結，依使用者指定的深度進行遞迴下載，並處理相對網址、外部網站限制、下載進度與中斷續傳等問題。

在基本功能完成後，另外實作多執行緒下載、檔案類型篩選與檔案大小限制等功能。

---

## Main Features

### HTTP 與網頁處理

- 使用 WinSock 建立網路連線
- 自行產生 HTTP GET Request
- 解析 HTTP Response Header / Body
- 處理 `Content-Length`
- HTML 連結解析
- 相對 URL 轉換為 Absolute URL
- 避免重複 URL 下載
- 支援指定遞迴深度
- 可選擇是否下載外部網站內容

### 下載控制

- Command-line Mode
- Interactive Mode
- 顯示目前下載物件資訊
- 顯示下載進度
- 顯示下載速度
- 顯示預估剩餘時間（ETA）
- 統計下載檔案數量
- 統計總下載容量與時間
- 支援停止與後續續傳

### Advanced Functions

- Multi-threaded Download
- 可設定同時下載的 Thread 數量
- 下載檔案類型篩選
- 最小檔案大小篩選
- HTTP Range Resume
- Thread-safe Global Statistics

---

## Program Architecture

程式依功能拆分為六個主要模組，使 HTTP crawling、檔案下載與使用者介面能分開處理。

| 檔案 | 主要功能 |
| --- | --- |
| `main.cpp` | 程式入口、使用者輸入、參數處理與整體流程控制 |
| `util.h` | URL 轉檔名、輸出目錄建立等共用工具 |
| `downloader.h` | DownloadOptions、GlobalStats 與 HttpDownloader 介面定義 |
| `downloader.cpp` | HTTP 連線、HTML 解析、URL 處理與遞迴 crawling |
| `advanced.h` | 多執行緒下載器、Task Queue 與 Worker 結構定義 |
| `advanced.cpp` | 多執行緒檔案下載、進度監控、續傳與檔案篩選 |

整體流程可簡化為：

```text
User Input
    │
    ▼
 main.cpp
    │
    ├───────────────┐
    ▼               ▼
HttpDownloader   AdvancedDownloader
    │               │
    │               ├─ Task Queue
    │               ├─ Worker Threads
    │               └─ Progress Monitor
    │
    ├─ HTTP GET
    ├─ Response Parsing
    ├─ HTML Parsing
    ├─ URL Resolution
    └─ Recursive Crawling
            │
            ▼
      Downloaded Files
```

---

## HTTP Implementation

本專題的其中一個核心限制，是不能直接使用現成 HTTP Client Library。

因此 HTTP Request 與 Response 處理由程式自行完成：

```text
URL
 │
 ▼
Parse Host / Path
 │
 ▼
WinSock Connection
 │
 ▼
HTTP GET Request
 │
 ▼
HTTP Response
 │
 ├─ Header
 │   └─ Content-Length
 │
 └─ Body
     ├─ HTML → 解析 Links
     └─ File → 下載
```

這個過程讓我實際接觸到應用層 HTTP 訊息與底層 Socket 連線之間的關係，而不只是透過既有函式庫取得網頁內容。

---

## Recursive Crawling

程式會解析 HTML 中的連結，並依設定決定是否繼續遞迴。

主要處理：

- `<a href>`
- `<img src>`
- Relative URL
- Absolute URL
- Same-host / External-host 判斷
- Maximum Depth
- Visited URL Set

例如：

```text
Start URL
│
├── Page A
│   ├── Image A
│   └── Page B
│       └── Image B
│
└── Page C
```

當 `maxDepth = 0` 時，只處理起始頁；提高深度後才會繼續解析下一層網頁。

---

## Multi-threaded Downloader

進階下載部分使用 Task Queue 管理待下載項目，並由多個 Worker Thread 同時執行下載。

```text
                 ┌─ Worker 1
                 │
Task Queue ──────├─ Worker 2
                 │
                 ├─ Worker 3
                 │
                 └─ Worker N
                        │
                        ▼
                  Download Files

               Monitor Thread
                    │
                    └─ Progress / Speed / ETA
```

共享的下載統計與 Queue 使用 Mutex 保護，以降低多執行緒同時讀寫造成的資料衝突。

---

## Reports

完整課程文件：

- [`Term_Project_Report.pdf`](./Term_Project_Report.pdf) — 程式架構、程式碼說明、操作方式、測試成果與心得

---

## What I Learned

這個專題讓我第一次從 Socket 層開始實作較完整的 HTTP 下載流程。

相較於直接使用既有 HTTP 函式庫，從 WinSock 建立連線、產生 GET Request、解析 Response，到後續處理 HTML、URL 與遞迴下載，使我更具體理解計算機網路課程中應用層協定與實際程式之間的關係。

在基本網路功能之外，加入 Task Queue、多執行緒下載、Mutex、進度監控與續傳後，也讓原本單純的網頁下載程式逐步成為具有模組分工與狀態管理的完整程式。

---

> 本 Repository 為大學課程期末專題成果整理，程式與報告以課程期間實際完成內容為主。
