// 避免這個檔案被include多次
#pragma once

#include <string>
#include <vector>
// 用 set 避免網址重複加入
#include <set>
// 避免多執行緒衝突
#include <mutex>

// 1. user 設定的下載選項
struct DownloadOptions {
    int  maxDepth = 0; // 遞迴深度(0 = 只抓起始頁)
    bool allowExternal = false; // 是否允許去抓外部網站
    int  resumeMode = 0; // 0 = 正常，1 = 續傳，2 = 刪掉就有紀錄重新下載
    int  showProgress = 0; // 是否顯示進度
    // user 限制下載的檔案類型，如果這邊 vector 是空的就要全部下載
    // 然後 HTML 也會計入 totalFiles 中
    std::vector<std::string> fileTypes;
};
// 2.全域統計的資訊（基礎 part + 進階 part 一起共用）
struct GlobalStats {
    long long totalBytes = 0; // 下載的 tatal bytes
    long long totalFiles = 0; // 下載的 total 檔案數量
    unsigned long long startTick = 0; // 開始下載的時間(算總耗時用)
    bool showProgress = true; // 是否顯示進度
    bool stopFlag = false; // 是否要停止下載

    std::mutex mtx; // 保護這些變數的 mutex
};
// 宣告 AdvancedDownloader 的類別，因為後面的 HttpDownloader 參數會用到
class AdvancedDownloader;
// 3.HttpDownloader 類別:用來解析 html 、爬超連結、抓 html 頁面
class HttpDownloader {
public: //建構子:設定起始的 URL、輸出的資料夾、下載的各個選項、全域統計之資料
    HttpDownloader(const std::string &startUrl,
                   const std::string &outDir,
                   const DownloadOptions &opt,
                   GlobalStats* stats);
    // 開始爬網頁(遞迴執行 crawl)，adv = 進階下載器，用來下載非 html 檔案
    void start(AdvancedDownloader* adv);
    // 把 link (相對路徑) 轉成絕對 URL
    // 像是 base = http://abc.com/docs/index.htm；
    // link = ../image/a.jpg；會回傳 http://abc.com/image/a.jpg
    std::string absoluteURL(const std::string &baseUrl,
                            const std::string &link);
    // 判斷這個 URL 網址是不是跟起始網址同一個 host (避免抓外部網站)
    bool isSameHost(const std::string &url);
    // 回傳起始網址的 host 部分，像是 hsccl.fr.to
    std::string getRootHost() const { return rootHost; }

private:
    // 解析 URL ，拆成各個部分: scheme, host, port, path
    // 像是http://abc.com:80/docs/a.htm
    // scheme = http；host = abc.com；port = 80；path = /docs/a.htm
    bool parseURL(const std::string &url,
                  std::string &scheme,
                  std::string &host,
                  int &port,
                  std::string &path);
    // 遞迴抓 HTML，depth = 目前深度
    void crawl(const std::string &url,
               int depth,
               AdvancedDownloader* adv);
    // 從 HTML 內容中找出所有連結， baseUrl = 這份 HTML 的 URL(用來處理相對路徑)
    void extractLinks(const std::string &baseUrl,
                      const std::string &html,
                      int depth,
                      AdvancedDownloader* adv);
    //下載 HTML 本體，url = 要下載的網址，body = 回傳 HTML 內容字串
    bool downloadPage(const std::string &url, std::string &body);
private:
    std::string rootUrl; // 起始 URL 網址(用來遞迴比較)
    std::string outDir; // 輸出資料夾
    std::string rootScheme; // http / https (作業只有支援 http，https 只是為了處理連結)
    std::string rootHost; // 起始 host (用來判斷有無抓外部網站)
    DownloadOptions opt; // user 所有下載選項(遞迴深度、續傳、檔案類型等等)
    std::set<std::string> visited; // 已經下載過的 URL 網址(避免重複下載)
    GlobalStats* stats; // 全域統計的資料(下載量、檔案數量等等)
};
