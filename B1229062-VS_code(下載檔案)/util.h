// 避免這個檔案被include多次
#pragma once

#include <string>
// 用在檔案/資料夾操作
#include <filesystem>

// 1. urlToFilename() : 把 URL 轉成合法當檔名的字串
// 避免名字衝突被覆蓋掉以及避免非法字源造成無法存檔
inline std::string urlToFilename(const std::string &url) {
    std::string u = url;
    // (1) 去掉 URL 的 scheme 部分 (http:// 或 https://)
    size_t p = u.find("://");
    if (p != std::string::npos)
        u = u.substr(p + 3); // +3 是跳過 "://" 從後面開始取字串
    // (2) 找到第一個斜線"/"確認有path
    // 像是 http://abc.com/docs/a.htm，會找到 docs/a.htm 這個 path
    size_t slash = u.find('/');
    if (slash == std::string::npos)
        return ""; // 沒有 path 就不下載，會無法當檔名會回傳空字串，像是 http://abc.com 這種
    std::string path = u.substr(slash + 1);
    if (path.empty()) return ""; // 有 "/" 但後面沒有東西，不當檔名
    // (3) 把所有 "/" 換成 "_"，改成安全的檔名
    // 像是 images/animals/cats/image.jpg ， images_animals_cats_image.jpg
    for (char &c : path) {
        if (c == '/') c = '_';
    }
    // (4) 不阻擋 html 檔案下載，但只抓 .htm ，不抓 .html (避免重複下載)
    // .html 負責解析網頁用，不當下載檔案
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        std::string ext = path.substr(dot + 1);
        for (char &c : ext) c = tolower(c);
        if (ext == "html") return ""; // 不抓 .html 檔案
    }
    // (5) 把網址後面的 query 去掉，避免檔名過長或有非法字元
    // 像是 image.jpg?version=1 會變成 image.jpg
    size_t q = path.find('?');
    if (q != std::string::npos)
        path = path.substr(0, q);
    // (6) 移除其他非法字元，像是:* , ?, ", <, >, | 
    for (char &c : path) {
        if (c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == '\\')
            c = '_';
    }
    // (7) 回傳安全的檔名字串
    return path;
}

// 2. 如果資料夾不存在會自動建立資料夾
inline void ensureDir(const std::string &path) {
    if (!std::filesystem::exists(path)) {
        // 可以建立多層資料夾
        std::filesystem::create_directories(path);
    }
}
