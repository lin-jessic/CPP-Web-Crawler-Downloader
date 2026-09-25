// AdvancedDownloader 的 class 定義
#include "advanced.h"
// urlToFilename() / ensureDir() 等工具
#include "util.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
// setw(), setprecision()
#include <iomanip>
// Windows socket API
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <conio.h>
#pragma comment(lib, "ws2_32.lib")

using namespace std;
namespace fs = std::filesystem;

// getExtLower2():傳入路徑或是 URL 最後檔名，抽出副檔名並轉小寫
// 像是 image.JPG -> "jpg"
static std::string getExtLower2(const std::string &path) {
    // (1)去掉 query string (?abc=1)
    size_t q = path.find('?');
    std::string p = (q == std::string::npos ? path : path.substr(0, q));
    // (2)找副檔名最後一個'.'
    size_t pos = p.find_last_of('.');
    if (pos == std::string::npos) return "";//沒副檔名回傳空字串
    // (3)抓副檔名並全部轉小寫
    std::string ext = p.substr(pos + 1);
    for (auto &c : ext) c = (char)tolower(c);
    return ext;
}
// 建構子:初始化成員之變數
AdvancedDownloader::AdvancedDownloader(int maxThreads,
                                       const std::string &outDir,
                                       const DownloadOptions &opt,
                                       GlobalStats* stats)
    : maxThreads(maxThreads), // user 設定的最大執行緒數
      outDir(outDir), // 下載儲存的資料夾
      minSize(0), // 預設檔案下限 = 0（也就是不限制）
      allowAllTypes(true), // 預設允許所有副檔名
      opt(opt), // 複製 user 設定
      stats(stats), // 全域統計的指標
      monitorStop(false) { // 監控執行緒停止的旗標
}
// 設定允許的副檔名
void AdvancedDownloader::setAllowTypes(const vector<string> &types) {
    allowTypes = types; // 儲存附檔名
    allowAllTypes = types.empty(); // 空的話，代表所有副檔名都允許
}
// 設定檔案下載的下限(byte)
void AdvancedDownloader::setMinSize(long long size) {
    minSize = size;
}
// addTask():新增一個要下載的URL
// 用 mutex 保護去避免多執行緒競爭，用 set 紀錄看過的 URL 去避免重複下載
void AdvancedDownloader::addTask(const string &url) {
    if (!stats) return; // 沒有統計的話不動
    if (stats->stopFlag) return; // 若 user 按 q，不再加入任務
    std::lock_guard<std::mutex> lock(taskMutex);
    // 避免重複下載同一個 URL
    if (taskSet.count(url)) {
        return;
    }
    taskSet.insert(url);
    tasks.push(url); // 加入下載 queue
}
// monitorLoop():負責顯示所有 worker thread 下載進度
// 每 0.2 秒更新一次(更新同一排文字，不會刷整個面) 
void AdvancedDownloader::monitorLoop(int threads) {
    if (!stats) return;
    if (!stats->showProgress) return;
    while (!monitorStop && !stats->stopFlag) {
        {
            lock_guard<mutex> lock(progressMutex);
            // 先清掉當前那一整行文字
            cout << "\r";
            cout << setw(120) << " ";
            cout << "\r";

            cout << "進度：";
            // 印出每個 worker 的狀態
            for (int i = 0; i < threads; ++i) {
                if (i >= (int)progress.size()) break;
                cout << " T" << (i + 1) << " ";
                if (progress[i].active) { // 印出百分比、速度、ETA
                    cout << setw(3) << progress[i].percent << "% "
                         << fixed << setprecision(1) << progress[i].speedKB << "KB/s ";
                    cout << "ETA " << fixed << setprecision(1)
                         << (progress[i].etaSec > 0 ? progress[i].etaSec : 0.0) << "s ";
                } else {
                    cout << "-- ";
                }
            }
            cout << flush;
        }
        Sleep(200); // 每 0.2 秒更新一次
    }

    // 結束前清掉最後一行
    if (stats->showProgress) {
        cout << "\r" << setw(120) << " " << "\r" << flush;
    }
}
// run():負責啟動所有 worker thread & Monnitor thread
void AdvancedDownloader::run() {
    if (!stats) return;
    vector<thread> workers; // 存 worker thread
    int threads = maxThreads;
    // 保護 thread 限制(最多=5)
    if (threads < 1) threads = 1;
    if (threads > 5) threads = 5;
    // 初始化每個 worker 的進度
    progress.assign(threads, ThreadProgress());
    monitorStop = false;
    // 若 user 有選擇顯示進度，開一個 monitor thread
    thread monitor;
    if (stats->showProgress) {
        monitor = thread(&AdvancedDownloader::monitorLoop, this, threads);
    }
    // 建立 worker threads (每個 thread 都會跑 workerThread(id))
    for (int i = 0; i < threads; i++) {
        workers.emplace_back(&AdvancedDownloader::workerThread, this, i);
    }
    // 等所有 worker 結束
    for (auto &t : workers) t.join();
    // 通知 monitor thread 停止
    monitorStop = true;
    if (stats->showProgress && monitor.joinable()) {
        monitor.join();
    }
}
// 每個 worker thread 都會跑這個函式，id = thread 編號
void AdvancedDownloader::workerThread(int id) {
    while (true) {
        // 如果按 q ，全部的 worker 都要停止
        if (!stats) return;
        if (stats->stopFlag) return;
        string url;
        // 取下一個任務(critical section)
        {
            lock_guard<mutex> lock(taskMutex);
            //如果 queue 空了，這個 worker 就結束
            if (tasks.empty()) return;
            url = tasks.front(); // 取出最前面的 URL
            tasks.pop(); // 從 queue 移除
        }
        // 把 URL 轉成本地檔名
        string filename = urlToFilename(url);
        // urlToFilename 可能回傳""(例如是 html 或不合法的)，要略過
        if (filename.empty()) {
            continue; // 不要當成下載任務
       }
        string saveFile = outDir + "/" + filename;
        // 副檔名(全小寫)
        string ext = getExtLower2(filename);
        // 檔案名篩選
        bool typeOK = false;
        if (allowAllTypes) {
            typeOK = true; //若沒有限制副檔名，全部都允許
        } else {
            for (auto &t : allowTypes) {
                if (ext == t) {
                    typeOK = true;
                    break;
                }
            }
        }
        if (!typeOK) {
            cout << "[略過類型] " << url << "\n";
            continue;
        }
        // 取得本地端已下載的大小(續傳用的)
        long long localSize = 0;
        if (fs::exists(saveFile))
            localSize = fs::file_size(saveFile);
        // 第一次連線:只為了取 Content-Length (遠端檔案大小)
        WSADATA wsa;
        WSAStartup(MAKEWORD(2,2), &wsa);
        string host, path;
        int port = 80;
        // 解析 URL :分割 host / path
        size_t p = url.find("://");
        string u = (p == string::npos ? url : url.substr(p+3));
        size_t slash = u.find('/');
        if (slash == string::npos) {
            host = u;
            path = "/";
        } else {
            host = u.substr(0, slash);
            path = u.substr(slash);
        }
        // 支援 URL : port
        string portStr = "80";
        size_t colon = host.find(':');
        if (colon != string::npos) {
            portStr = host.substr(colon+1);
            host    = host.substr(0, colon);
        }
        // 域名轉 IP
        struct addrinfo hints{}, *res;
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) {
            cout << "[連線失敗] " << url << "\n";
            WSACleanup();
            continue;
        }
        
        SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (connect(s, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
            cout << "[連線失敗] " << url << "\n";
            freeaddrinfo(res);
            closesocket(s);
            WSACleanup();
            continue;
        }
        freeaddrinfo(res);
        // 發送 GET，抓 header
        string req = "GET " + path + " HTTP/1.1\r\n";
        req += "Host: " + host + "\r\n";
        req += "Connection: close\r\n\r\n";
        send(s, req.c_str(), (int)req.size(), 0);
        string header;
        char buf[4096];
        long long remoteSize = -1;
        // 收 header
        while (true) {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0) break;
            header.append(buf, n);
            size_t pos = header.find("\r\n\r\n");
            if (pos != string::npos) {
                // 找 Content-Length
                string h = header.substr(0, pos+4);
                size_t cp = h.find("Content-Length:");
                if (cp != string::npos) {
                    cp += 15;
                    while (cp < h.size() && isspace((unsigned char)h[cp])) cp++;
                    remoteSize = atoll(&h[cp]);
                }
                break;
            }
        }

        closesocket(s);
        WSACleanup();

        // 續傳 / 略過判斷
        bool doResume = false;
        // 續傳模式 (resumeMode = 1)，且小於 remoteSize，執行續傳
        if (opt.resumeMode == 1 &&
            remoteSize > 0 &&
            localSize > 0 &&
            localSize < remoteSize) {
            doResume = true;
            cout << "[續傳檔案] " << url << "\n";
        }
        else if (opt.resumeMode == 1 &&
                 remoteSize > 0 &&
                 localSize >= remoteSize) {
            // 本地已經下載完成
            cout << "[略過已完成檔案] " << saveFile << "\n";
            continue;
        }

        // 第二次連線:開始正式下載檔案
        WSAStartup(MAKEWORD(2,2), &wsa);

        struct addrinfo *res2;
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res2) != 0) {
            cout << "[連線失敗] " << url << "\n";
            WSACleanup();
            continue;
        }

        SOCKET s2 = socket(res2->ai_family, res2->ai_socktype, res2->ai_protocol);
        if (connect(s2, res2->ai_addr, (int)res2->ai_addrlen) == SOCKET_ERROR) {
            cout << "[連線失敗] " << url << "\n";
            freeaddrinfo(res2);
            closesocket(s2);
            WSACleanup();
            continue;
        }
        freeaddrinfo(res2);
        string req2;
        ofstream fout;
        long long got = localSize; // 已經下載的大小(續傳用)
        if (doResume) { // 有 Range 的續傳 GET
            req2 = "GET " + path + " HTTP/1.1\r\n";
            req2 += "Host: " + host + "\r\n";
            req2 += "Range: bytes=" + to_string(localSize) + "-\r\n";
            req2 += "Connection: close\r\n\r\n";
            fout.open(saveFile, ios::binary | ios::app);
        } else { // 一般下載(從頭開始)
            req2 = "GET " + path + " HTTP/1.1\r\n";
            req2 += "Host: " + host + "\r\n";
            req2 += "Connection: close\r\n\r\n";
            fout.open(saveFile, ios::binary | ios::trunc);
            got = 0;
        }

        send(s2, req2.c_str(), (int)req2.size(), 0);
        bool headerDone = false;
        string recvHeader;
        unsigned long long fileStart = GetTickCount64();

        cout << "[檔案] 下載 " << url << endl;
        // 下載主迴圈
        while (true) {
            if (stats->stopFlag) break;

            // 按 q 停止，設 stopFlag
            if (_kbhit()) {
                char c = _getch();
                if (c == 'q' || c == 'Q') {
                    stats->stopFlag = true;
                }
            }

            int n = recv(s2, buf, sizeof(buf), 0);
            if (n <= 0) break;
            // 處理header
            if (!headerDone) {
                recvHeader.append(buf, n);
                size_t pos = recvHeader.find("\r\n\r\n");
                if (pos != string::npos) {
                    int remain = n - (int)(pos + 4);
                    if (remain > 0) {
                        // header 後面的資料寫進檔案
                        fout.write(buf + pos + 4, remain);
                        got += remain;
                    }
                    headerDone = true;
                }
            } else { // header 已完成，全部都是檔案內容
                fout.write(buf, n);
                got += n;
            }

            // 更新下載進度，由 monitorLoop 統一印一行
            if (stats->showProgress && remoteSize > 0) {
                unsigned long long now = GetTickCount64();
                double elapsed = (now - fileStart) / 1000.0;
                if (elapsed <= 0.0) elapsed = 0.001;
                double speedKB = (got - localSize) / 1024.0 / elapsed;

                int pct = (int)(100.0 * got / remoteSize);
                if (pct > 100) pct = 100;

                double eta = 0.0;
                if (got > 0) {
                    double left = (double)remoteSize - got;
                    eta = left / ((got - localSize) / elapsed + 1e-6);
                }

                lock_guard<mutex> lock(progressMutex);
                if (id < (int)progress.size()) {
                    progress[id].active = true;
                    progress[id].percent = pct;
                    progress[id].speedKB = speedKB;
                    progress[id].etaSec = eta;
                }
            }
        }
        fout.close();
        closesocket(s2);
        WSACleanup();
        // 檔案下載完畢，標記 worker 空閒（給 monitor thread 知道）
        if (stats->showProgress) {
            lock_guard<mutex> lock(progressMutex);
            if (id < (int)progress.size()) {
                progress[id].active = false;
            }
        }

        // 檔案大小限制判斷（下限）
        if (minSize > 0 && got < minSize) {
            cout << "    [刪除] " << filename << " (" << got
                 << " bytes) 小於下限 " << minSize << "\n";
            fs::remove(saveFile);
            continue;
        }

        // 更新全域統計(總檔案數、總 byte 數)
        {
            std::lock_guard<std::mutex> lock(stats->mtx);
            stats->totalBytes += (got - localSize); // 新增下載的 bytes
            stats->totalFiles += 1; // 完成檔案 + 1

            double mb = stats->totalBytes / 1024.0 / 1024.0;
            double elapsedTotal = (GetTickCount64() - stats->startTick) / 1000.0;

            cout << "\n";
            cout << "    [統計] 已完成檔案數: " << stats->totalFiles
                 << "  已下載: " << fixed << setprecision(3) << mb << " MB"
                 << "  總耗時: " << fixed << setprecision(1)
                 << elapsedTotal << " 秒\n";
        }
    }
}
