// 避免這個檔案被include多次
#pragma once

#include <string>
#include <vector>
#include <queue>

// 避免多執行緒衝突
#include <mutex>
// 用 set 避免網址重複加入
#include <set>
// 需要 DownloadOptions / GlobalStats
#include "downloader.h"

// 1.每個 worker 的進度資訊(顯示用的)
struct ThreadProgress {
    int percent = 0; // 下載的百分比(0%~100%)
    double speedKB = 0.0; // 下載的速度(KB/s)
    double etaSec  = 0.0; // 預估剩餘秒數
    bool active  = false; // 這個 worker 目前有無在下載
};
// 2.多執行緒下載器:用來抓圖片/PDF/其他更多檔案
class AdvancedDownloader {
public: //建構子:指定最大執行緒數量、輸出之資料夾，等使用者設定
    AdvancedDownloader(int maxThreads,
                       const std::string &outDir,
                       const DownloadOptions &opt,
                       GlobalStats* stats);

    // 加入一下載任務(URL網址)，會放進 tasks queue 中
    void addTask(const std::string &url);

    // 限制只下載哪些副檔名(我的 code 是限制要小寫、也不要.，像是直接打 jpg)
    void setAllowTypes(const std::vector<std::string> &types);

    // 設定檔案大小下限(byte)，0 表示不會去限制
    void setMinSize(long long size);

    // 開始執行所有下載任務，會開 maxThreads 個 worker thread
    void run();

private:
    void workerThread(int id); // 每個 worker thread 會做的工作(負責下載一個檔案)
    void monitorLoop(int threads);  // 監控 thread，定期印出所有多執行緒進度
private:
    int maxThreads; // 同時允許的執行緒數量
    std::string outDir; // 下載輸出的資料夾
    std::queue<std::string> tasks; // tasks queue(要下載的URL)
    std::mutex taskMutex; // tasks queue 的 mutex，避免多 thread 同時存取衝突  
    std::set<std::string> taskSet; // 避免重複加入相同 URL 進 tasks queue
    std::vector<std::string> allowTypes; // 允許下載的副檔名清單
    long long minSize; // 檔案霸小下限(byte)
    bool allowAllTypes; // true=允許下載所有副檔名、false=有限制
    DownloadOptions opt; // user 所有下載選項
    GlobalStats* stats; // 全域統計的資料(下載量、檔案數量等等)
    // 多thread 進度顯示相關
    std::vector<ThreadProgress> progress; // 每個 worker 的進度
    std::mutex progressMutex; // 保護 progress 的 mutex
    bool monitorStop = false; // 是否要停止 monitor thread
};
