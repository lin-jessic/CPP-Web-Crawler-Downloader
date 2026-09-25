// HttpDownloader 類別定義
#include "downloader.h"
// AdvancedDonloader，多執行緒下載器
#include "advanced.h"
// urlToFilename 等工具函式
#include "util.h"

//Window socket 必備
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <conio.h>
// 連 socket 函式庫
#pragma comment(lib, "ws2_32.lib")

#include <fstream>
#include <iostream>
#include <filesystem>
// 使用 regex 找 href/src
#include <regex>
// setw/format 用
#include <iomanip>

using namespace std;
namespace fs = std::filesystem;

// getExtLower:小工具，從 URL 裡抓出路徑的副檔名(小寫、沒有點)
static std::string getExtLower(const std::string &url) {
    // 1.去掉 http:// 或 https://
    size_t p = url.find("://");
    size_t start = (p == string::npos ? 0 : p + 3);
    // 2.找出 path
    size_t slashPos = url.find('/', start);
    string path = (slashPos == string::npos ? "/" : url.substr(slashPos));
    // 3.去掉 ? 後面的 query string
    size_t q = path.find('?');
    if (q != string::npos)
        path = path.substr(0, q);
    // 4.最後一個點後視為副檔名
    size_t dot = path.find_last_of('.');
    if (dot == string::npos) return "";
    string ext = path.substr(dot + 1);
    // 5.轉小寫
    for (char &c : ext) c = tolower(c);
    return ext;
}
// isHtmlLike:判斷 URL 是否應該當作 HTML 下載/解析
static bool isHtmlLike(const std::string &url) {
    std::string ext = getExtLower(url);
    // 只有 .htm 才算 HTML(避免重複下載)
    if (ext == "htm") return true;
    // path 有斜線但沒有副檔名，是資料夾，回傳 true（HTML）
    // 例如 http://hsccl.us.to/
    // 但是不能把沒有 slash 的網址當 HTML！
    size_t p = url.find("://");
    size_t start = (p == std::string::npos ? 0 : p + 3);
    size_t slash = url.find('/', start);
    // 沒有 path '/'，不能當 HTML（例如 http://hsccl.fr.to）
    if (slash == std::string::npos) return false;
    // 如果 path 存在但沒有副檔名， HTML 資料夾
    if (ext == "") return true;

    return false;
}
// rewriteHTML:將 HTML 內的所有 src/href 連結改成本地檔名(離線瀏覽使用)
// 像是 img src="images/cat1.jpg" 變成 img src="images_cat1.jpg"
static std::string rewriteHTML(const std::string& html,
                               const std::string& baseUrl,
                               HttpDownloader* dl)
{
    std::string out = html;
    // 抓出所有 src="xxx" 或 href="xxx"
    std::regex rg("(src|href)\\s*=\\s*\"([^\"]+)\"", std::regex::icase);
    std::smatch m;

    std::string result;
    std::string::const_iterator it = html.begin();
    std::string::const_iterator ed = html.end();

    while (std::regex_search(it, ed, m, rg)) {
        std::string fullMatch = m[0]; // 整段 src="xxx"
        std::string link = m[2].str(); // xxx
        // 轉成絕對網址
        std::string abs = dl->absoluteURL(baseUrl, link);
        // 變成本地檔名
        std::string local = urlToFilename(abs);
        // 如果連結是目錄(沒有檔名)就不要改
        if (local.empty()) {
            it = m[0].second;
            continue;
        }
        // 用本地檔名取代
        std::string replaced = m[1].str() + "=\"" + local + "\"";
        out = std::regex_replace(out, std::regex(fullMatch), replaced);

        it = m[0].second;
    }

    return out;
}
// HttpDowloader 建構子
HttpDownloader::HttpDownloader(const std::string &startUrl,
                               const std::string &outDir,
                               const DownloadOptions &opt,
                               GlobalStats* stats)
    : rootUrl(startUrl),
      outDir(outDir),
      opt(opt),
      stats(stats)
{
    string path;
    int port;
    // 解析成 rootScheme / rootHost (用來判斷外網)
    parseURL(startUrl, rootScheme, rootHost, port, path);
}
// parseURL :解析 URL
// 把 URL 分成 scheme(http), host(domain), port, path(/abc/xxx.htm)
bool HttpDownloader::parseURL(const std::string &url,
                              std::string &scheme,
                              std::string &host,
                              int &port,
                              std::string &path)
{
    scheme = "http";
    port   = 80;
    host.clear();
    path = "/";
    // 1.找"://"
    size_t p = url.find("://");
    size_t start = (p == string::npos ? 0 : p + 3);
    // 2. 找到第一個 '/'
    size_t slash = url.find('/', start);
    string hostPort;
    if (slash == string::npos) {
        //沒有 path 的情況
        hostPort = url.substr(start);
        path = "/";
    } else {
        hostPort = url.substr(start, slash - start);
        path = url.substr(slash);
    }
    // 看有沒有 port
    size_t colon = hostPort.find(':');
    if (colon == string::npos) {
        host = hostPort;
    } else {
        host = hostPort.substr(0, colon);
        port = stoi(hostPort.substr(colon + 1));
    }
    return !host.empty();
}

// isSameHost :判斷是否屬於內網(相同 host)
bool HttpDownloader::isSameHost(const std::string &url) {
    string s, h, path;
    int port;
    parseURL(url, s, h, port, path);
    return (h == rootHost);
}
// absoluteURL :把 link 變成絕對 URL
// 像是 /abc/cat.jpg 變成 http://host/abc/cat.jpg
string HttpDownloader::absoluteURL(const string &baseUrl,
                                   const string &link)
{
    if (link.empty()) return "";
    // 已經是絕對網址
    if (link.rfind("http://", 0) == 0 ||
        link.rfind("https://", 0) == 0)
        return link;

    string s, h, path;
    int port;
    parseURL(baseUrl, s, h, port, path);
    // 以根目錄開頭
    if (link[0] == '/') {
        return s + "://" + h + link;
    }
    // 否則在 baseUrl 所在的資料夾下組合
    size_t pos = path.find_last_of('/');
    string dir = (pos == string::npos ? "/" : path.substr(0, pos + 1));
    return s + "://" + h + dir + link;
}
// start:開始遞迴下載
void HttpDownloader::start(AdvancedDownloader *adv) {
    ensureDir(outDir); // 確保資料夾存在
    visited.clear(); // 清空以抓取紀錄(避免重複)
    crawl(rootUrl, 0, adv); // 從起始 URL 開始抓
}
// crawl (遞迴核心):主 HTML 遞迴
void HttpDownloader::crawl(const string &url,
                           int depth,
                           AdvancedDownloader *adv)
{
    if (!stats || stats->stopFlag) return;
    // 重複網址，不要抓
    if (visited.count(url)) return;
    visited.insert(url);
    // 不是 HTML ，丟給進階多執行緒下載器(圖片、PDF)
    if (!isHtmlLike(url)) {
        if (adv) adv->addTask(url);
        return;
    }
    // 是HTML ，抓內容（不寫檔）
    string html;
    if (!downloadPage(url, html)) return;
    // 抽取內部連結
    extractLinks(url, html, depth, adv);
    // 深度限制
    if (opt.maxDepth == 0) return;
    if (depth >= opt.maxDepth) return;
}
// extractLinks:負責從 HTML 找所有 link ，遞迴爬下去
void HttpDownloader::extractLinks(const string &baseUrl,
                                  const string &html,
                                  int depth,
                                  AdvancedDownloader *adv)
{
    regex rg("href\\s*=\\s*\"([^\"]+)\"|src\\s*=\\s*\"([^\"]+)\"",
             regex::icase);
    smatch m;

    string::const_iterator it = html.begin();
    string::const_iterator ed = html.end();

    while (regex_search(it, ed, m, rg)) {
        string link = m[1].matched ? m[1].str() : m[2].str();
        string abs = absoluteURL(baseUrl, link);
        if (abs.empty()) {
            it = m[0].second;
            continue;
        }
        // 不允許外站
        if (!opt.allowExternal && !isSameHost(abs)) {
            it = m[0].second;
            continue;
        }
        bool htmlLike = isHtmlLike(abs);
        if (htmlLike) {
            // 遞迴下去
            if (opt.maxDepth > 0 && depth < opt.maxDepth)
                crawl(abs, depth + 1, adv);
        }
        else {
            // 加入下載任務(圖片等等)
            if (adv) adv->addTask(abs);
        }

        it = m[0].second;
    }
}
// downloadPage:下載單一 HTML 本體（只解析，不寫成檔案）
bool HttpDownloader::downloadPage(const std::string &url, std::string &body) {
    body.clear(); // HTML 內容先清空
    if (!stats) return false; // 沒有統計，不下載
    // 1.用 parseURL 分解 URL -> scheme host port path
    string scheme, host, path;
    int port;
    if (!parseURL(url, scheme, host, port, path)) {
        cout << "[URL 錯誤] " << url << "\n";
        return false;
    }
    // 2. 第一次連線:只抓 header，取 Content-Length
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    addrinfo hints{}, *res;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    string portStr = to_string(port);
    // DNS 查詢
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) {
        cout << "[連線失敗] " << host << "\n";
        WSACleanup();
        return false;
    }
    // 建立 socket + connect
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        cout << "[連線失敗] " << host << "\n";
        freeaddrinfo(res);
        closesocket(s);
        WSACleanup();
        return false;
    }
    freeaddrinfo(res);
    // 發送 GET header
    string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Connection: close\r\n\r\n";

    send(s, req.c_str(), (int)req.size(), 0);
    // 讀 header，找 Content-Length
    string header;
    char buf[4096];
    long long remoteSize = -1; // 預設沒有給大小

    while (true) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        header.append(buf, n);
        size_t pos = header.find("\r\n\r\n");
        if (pos != string::npos) {

            string h = header.substr(0, pos + 4);

            regex cl("Content-Length:\\s*([0-9]+)", regex::icase);
            smatch m;
            if (regex_search(h, m, cl)) {
                remoteSize = stoll(m[1].str()); // 取得 HTML 大小
            }
            break;
        }
    }
    // 關閉第一次 socket
    closesocket(s);
    WSACleanup();

    // 3.第二次連線:正式下載(實際取) HTML body
    // 同樣流程: DNS -> connet
    WSAStartup(MAKEWORD(2,2), &wsa);
    addrinfo *res2;

    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res2) != 0) {
        cout << "[連線失敗] " << host << "\n";
        WSACleanup();
        return false;
    }

    SOCKET s2 = socket(res2->ai_family, res2->ai_socktype, res2->ai_protocol);
    if (connect(s2, res2->ai_addr, (int)res2->ai_addrlen) == SOCKET_ERROR) {
        cout << "[連線失敗] " << host << "\n";
        freeaddrinfo(res2);
        closesocket(s2);
        WSACleanup();
        return false;
    }
    freeaddrinfo(res2);
    // 發送 GET
    string req2 =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Connection: close\r\n\r\n";
    send(s2, req2.c_str(), (int)req2.size(), 0);
    // 開始下載 HTNL 主內容(body)
    // headerDone = 0，還在解析 header
    // headerDone = 1，開始把資料寫進 body
    bool headerDone = false;
    string recvHeader;
    long long got = 0;
    unsigned long long fileStart = GetTickCount64();

    // 3_1.決定要不要顯示 [HTML] 下載
    // 如果 user 沒有限制副檔名，HTML 頁算是要下載的檔案
    bool showHtmlLog = false;
    if (opt.fileTypes.empty()) {
        // 沒有限制，HTML 也算要下載的，顯示出來
        showHtmlLog = true;
    } else {
        // 有限制，只有使用者有填 html / htm 才會顯示
        for (auto t : opt.fileTypes) {
            for (char &c : t) c = tolower(c);
            if (t == "html" || t == "htm") {
                showHtmlLog = true;
                break;
            }
        }
    }
    if (showHtmlLog) {
        cout << "\n[HTML] 下載 " << url << "\n";
    }
    // 3_2.下載迴圈(解析 header + 抓 HTML body)
    while (true) {
        if (stats->stopFlag) break;
        // 支援 q 中止
        if (_kbhit()) {
            char c = _getch();
            if (c == 'q' || c == 'Q') {
                stats->stopFlag = true;
            }
        }
        int n = recv(s2, buf, sizeof(buf), 0);
        if (n <= 0) break;

        if (!headerDone) {
            // 拚 header
            recvHeader.append(buf, n);
            size_t pos = recvHeader.find("\r\n\r\n");
            if (pos != string::npos) {
                int remain = n - (int)(pos + 4);
                // header 後面的資料 -> body
                if (remain > 0) {
                    body.append(buf + pos + 4, remain);
                    got += remain;
                }
                headerDone = true;
            }
        } else {
            // header 完成後，全部是 HTML body
            body.append(buf, n);
            got += n;
        }
        // 顯示 HTML 的下載進度(不影響檔案)
        if (stats->showProgress) {
            unsigned long long now = GetTickCount64();
            double elapsed = (now - fileStart) / 1000.0;
            if (elapsed <= 0) elapsed = 0.001;

            double speedKB = got / 1024.0 / elapsed;

            int pct = 0;
            if (remoteSize > 0) {
                pct = (int)(100.0 * got / remoteSize);
                if (pct > 100) pct = 100;
            }

            double eta = 0;
            if (remoteSize > 0 && got > 0) {
                double left = (double)remoteSize - got;
                eta = left / (got / elapsed + 1e-6);
            }

            cout << "\r    "
                 << setw(3) << pct << "%  "
                 << fixed << setprecision(1) << speedKB << "KB/s  "
                 << "ETA " << fixed << setprecision(1)
                 << (eta > 0 ? eta : 0.0) << "s    " << flush;
        }
    }
        // 關閉 socket
        closesocket(s2);
        WSACleanup();

    if (stats->showProgress) cout << "\n";
    // 4.決定這個 HTML 要不要當成真正下載的檔案
    // 訂定的規則：
    // (1)沒有限制副檔名:HTML 也當成一般檔案
    // (2)有限制，且包含 html / htm:HTML 也算
    // (3)其他情況:HTML 只拿來解析連結，不存檔、不計數
    bool needHtmlFile = false;
    if (opt.fileTypes.empty()) {
        // 使用者沒有設定檔案類型，全部物件都要
        needHtmlFile = true;
    } else {
        for (auto t : opt.fileTypes) {
            for (char &c : t) c = (char)tolower((unsigned char)c);
            if (t == "html" || t == "htm") {
                needHtmlFile = true;
                break;
            }
        }
    }
    // 如果這個 HTML 不需要當成檔案，就不要存檔、不更新統計
    // 只保留在 body 字串裡讓上層去 extractLinks()
    if (!needHtmlFile) {
        return !stats->stopFlag;
    }
    // 5.真的需要 HTML 檔案寫成本地檔案(含 rewrite)
    {
        std::string rewritten = rewriteHTML(body, url, this);
        // (1)產生檔名，將網址解析 host + path
        //有 filename，用 urlToFile，沒有 filename (外網)，用 host_index.htm
        // 避免 .htm 被覆蓋
        std::string scheme, host, path;
        int port;
        parseURL(url, scheme, host, port, path);
        // 取得最後檔名
        std::string fname = urlToFilename(url);
        // 如果是資料夾 (沒有檔名)
        if (fname.empty()) {
            // 外網不能全部叫 index.htm，不然會彼此覆蓋
            // 所以加上 domain 作為前綴
            fname = host + "_index.htm";
        }
        // (2)過濾掉不合法字元
        for (char &c : fname) {
            if (c == '/' || c == '\\' || c == ':' ||
                c == '*' || c == '?' || c == '\"' ||
                c == '<' || c == '>' || c == '|' )
                c = '_';
        }
        // (3)寫檔
        std::string saveFile = outDir + "/" + fname;
        std::ofstream fout(saveFile, std::ios::binary);
        if (fout) {
            fout.write(rewritten.data(), (std::streamsize)rewritten.size());
        }
    }
    // 6.更新統計資訊(HTML 也算檔案)
    {
        std::lock_guard<std::mutex> lock(stats->mtx);

        stats->totalBytes += got;
        stats->totalFiles += 1;

        double mb = stats->totalBytes / 1024.0 / 1024.0;
        double elapsedTotal =
            (GetTickCount64() - stats->startTick) / 1000.0;

        std::cout << "    [統計] 已完成檔案數: " << stats->totalFiles
                  << "  已下載: " << std::fixed << std::setprecision(3) << mb << " MB"
                  << "  總耗時: " << std::fixed << std::setprecision(1)
                  << elapsedTotal << " 秒\n";
    }

    return !stats->stopFlag;
}
