#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
// Windows API 設定 UTF-8 編碼、計時
#include <windows.h>

// HTML 下載器，負責爬蟲
#include "downloader.h"
// 進階下載器，負責多 thread 下載(圖片/ PDF /其他檔案)
#include "advanced.h"
// 工具(urlToFilename / ensureDir)
#include "util.h"

using namespace std;
// 避免每次都要打 std::filesystem，用 fs 代替
namespace fs = std::filesystem;

// 1.判斷字串是不是數字，用來判斷 size
bool isNumber(const std::string &s) {
    if (s.empty()) return false; // 空字串不是數字
    for (char c : s)
        // 只要有不是數字的字元就回傳 false
        if (!isdigit((unsigned char)c)) return false;
    return true;
}
// 2. 刪除整個下載資料夾資料
void clearDownloadFolder(const std::string &outDir) {
    if (fs::exists(outDir)) { // 會先檢查資料夾存不存在
        cout << ">> 刪除舊的下載資料夾: " << outDir << "\n";
        fs::remove_all(outDir); // 刪除整個資料夾及底下所有檔案
    }
}
int main(int argc, char* argv[]) {
    //設定 Windows Console 使用 UTF-8 ，避免亂碼
    SetConsoleOutputCP(CP_UTF8); // cout 為 UTF-8 
    SetConsoleCP(CP_UTF8); // cin 為 UTF-8

    // 3.命令列模式 - 一次性輸入模式
    if (argc >= 8) { // 至少要8參數才能進入命令列模式
        string url = argv[1]; // 起始 URL
        string outDir = argv[2]; // 輸出資料夾名稱
        int depth = atoi(argv[3]); // 遞迴深度
        bool allowExt = (argv[4][0] == 'y' || argv[4][0] == 'Y'); // 允許外部網站
        bool showProgress = (argv[5][0] == 'y' || argv[5][0] == 'Y'); // 顯示進度
        int resumeMode = atoi(argv[6]); // 下載模式
        int maxThreads = atoi(argv[7]); // 同時下載檔案數上限
        vector<string> allowTypes; // user 限制要下載的副檔名
        long long minSize = 0; // user 限制檔案大小下限

        // 後面參數:若是數字視為 minSize，否則視為副檔名
        for (int i = 8; i < argc; ++i) {
            string arg = argv[i];
            if (isNumber(arg) && minSize == 0) {
                minSize = atoll(arg.c_str()); // 轉成 long long
            } else {
                allowTypes.push_back(arg); // 副檔名加入 vector
            }
        }
        // 準備 DownloadOptions 結構(user 設定的所有下載選項)
        // 要傳給 HttpDownloader / AdvancedDownloader 使用
        DownloadOptions opt;
        opt.maxDepth = depth;
        opt.allowExternal = allowExt;
        opt.resumeMode = resumeMode;
        opt.showProgress = showProgress ? 1 : 0;
        opt.fileTypes = allowTypes;
        // 若選擇刪除後重抓，先刪除舊資料夾
        if (resumeMode == 2) clearDownloadFolder(outDir);
        // 若資料夾不存在就建立
        ensureDir(outDir);
        // 建立 GlobalStats 結構(全域統計資訊)
        // 紀錄下載量、檔案數量等等
        GlobalStats stats;
        stats.totalBytes = 0;
        stats.totalFiles = 0;
        stats.startTick = GetTickCount64(); // 下載開始時間
        stats.showProgress = showProgress; // 是否顯示進度
        stats.stopFlag = false; // 是否停止下載(按q)

        cout << "\n------------------ {命令列模式} ------------------\n";
        //建立下載器物件
        opt.fileTypes = allowTypes;
        AdvancedDownloader adv(maxThreads, outDir, opt, &stats);
        if (!allowTypes.empty()) adv.setAllowTypes(allowTypes);
        if (minSize > 0)         adv.setMinSize(minSize);

        HttpDownloader dl(url, outDir, opt, &stats);
        // 主要下載流程(HTML -> 檔案 -> 檔案 ......)
        while (true) {
            dl.start(&adv); // 先下載 HTML　+　解析連結
            adv.run(); // 再由多 thread 下載所有檔案

            if (!stats.stopFlag) break; // 沒被 q 中止，就跳出迴圈結束下載

            // 被 q 中止
            cout << "\n偵測到下載中止，要繼續下載嗎？(y/n): ";
            char c;
            cin >> c;
            if (c == 'y' || c == 'Y') {
                stats.stopFlag = false;
                cout << "\n== 繼續下載 ==\n";
                continue; // 再進行下一輪下載
            } else {
                cout << "\n== 使用者選擇結束下載 ==\n";
                break;
            }
        }
        // 顯示下載統計結果
        double mb = stats.totalBytes / 1024.0 / 1024.0;
        double elapsedTotal = (GetTickCount64() - stats.startTick) / 1000.0;

        cout << "\n================== 下載流程結束 ==================\n";
        cout << "總共完成檔案數: " << stats.totalFiles
             << "  總下載量: " << fixed << setprecision(3) << mb << " MB"
             << "  總耗時: " << fixed << setprecision(1)
             << elapsedTotal << " 秒\n";

        return 0;
    }

    // 互動模式
    cout << "\n---------------------------------------------------\n";
    cout << "               {網路下載器 - 互動模式}\n";
    cout << "---------------------------------------------------\n\n";
    string url, outDir;
    DownloadOptions opt;

    cout << "[基本設定]\n";
    cout << "  請輸入 URL: ";
    cin >> url;

    cout << "  輸出資料夾名稱: ";
    cin >> outDir;

    cout << "  遞迴下載深度 (0 = 只抓起始頁): ";
    cin >> opt.maxDepth;

    char ch;

    cout << "\n[網域設定]\n";
    cout << "  允許外部網站？ (y/n): ";
    cin >> ch;
    opt.allowExternal = (ch == 'y' || ch == 'Y');

    cout << "\n[續傳 / 重抓]\n";
    cout << "  下載模式 (0=正常 1=續傳 2=刪除後重抓): ";
    cin >> opt.resumeMode;
    // 重抓，刪除目標舊資料夾
    if (opt.resumeMode == 2) clearDownloadFolder(outDir);
    // 存不存在就建立資料夾
    ensureDir(outDir);

    cout << "\n[顯示選項]\n";
    cout << "  顯示下載進度與統計？ (y/n): ";
    cin >> ch;
    bool showProgress = (ch == 'y' || ch == 'Y');
    opt.showProgress  = showProgress ? 1 : 0;

    int maxThreads;
    cout << "\n[多執行緒]\n";
    cout << "  同時下載檔案數上限 (1~5): ";
    cin >> maxThreads;

    vector<string> allowTypes;
    long long minSize = 0;

    cout << "\n[檔案限制]\n";
    cout << "  只下載特定檔案類型？ (y/n): ";
    cin >> ch;
    if (ch == 'y' || ch == 'Y') {
        cout << "  請輸入副檔名（例如: jpg png pdf），以空白分隔：";
        string ext;
        while (cin >> ext) {
            allowTypes.push_back(ext);
            if (cin.peek() == '\n') break;
        }
    }

    cout << "  設定檔案大小下限？ (y/n): ";
    cin >> ch;
    if (ch == 'y' || ch == 'Y') {
        cout << "  最小大小（byte）: ";
        cin >> minSize;
    }
    // 初始化 GlobalStats (儲存所有下載統計資訊)
    GlobalStats stats;
    stats.totalBytes = 0;
    stats.totalFiles = 0;
    stats.startTick = GetTickCount64();
    stats.showProgress = showProgress;
    stats.stopFlag = false;

    cout << "\\n================== 下載流程開始 ==================\n";
    // 建立下載物件 (HTML 下載器 + 進階多執行緒下載器)
    opt.fileTypes = allowTypes;
    AdvancedDownloader adv(maxThreads, outDir, opt, &stats);
    if (!allowTypes.empty()) adv.setAllowTypes(allowTypes);
    if (minSize > 0)         adv.setMinSize(minSize);

    HttpDownloader dl(url, outDir, opt, &stats);
    // 主要下載流程: HTML -> 多執行緒下載
    while (true) {
        dl.start(&adv); // 下載 HTML 並解析連結
        adv.run(); // 多執行緒下載所有檔案

        if (!stats.stopFlag) break; // 若沒有按 q ，跳出迴圈結束下載

        cout << "\n偵測到下載中止，要繼續下載嗎？(y/n): ";
        cin >> ch;
        if (ch == 'y' || ch == 'Y') {
            stats.stopFlag = false;
            cout << "\n== 繼續下載 ==\n";
            continue;
        } else {
            cout << "\n== 使用者選擇結束下載 ==\n";
            break;
        }
    }
    // 計算並輸出最後下載統計結果
    double mb = stats.totalBytes / 1024.0 / 1024.0;
    double elapsedTotal = (GetTickCount64() - stats.startTick) / 1000.0;

    cout << "\n================== 下載流程結束 ==================\n";
    cout << "總共完成檔案數: " << stats.totalFiles
         << "  總下載量: " << fixed << setprecision(3) << mb << " MB"
         << "  總耗時: " << fixed << setprecision(1)
         << elapsedTotal << " 秒\n";

    return 0;
}