#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <curl/curl.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "cJSON.h"

static void getRedfieldDir(char* out, size_t size) {
    const char* home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    if (!home) home = ".";
    snprintf(out, size, "%s/.prism", home);
}

static int fileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void mkdirRecursive(const char* path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            *p = '/';
        }
    }
#ifdef _WIN32
    _mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
}

static int saveToFile(const char* path, const char* data) {
    FILE* file = fopen(path, "w");
    if (!file) return 0;
    fputs(data, file);
    fclose(file);
    return 1;
}

struct FetchBuffer {
    char* data;
    size_t size;
};

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realSize = size * nmemb;
    struct FetchBuffer* buf = (struct FetchBuffer*)userp;
    char* ptr = realloc(buf->data, buf->size + realSize + 1);
    if (!ptr) return 0;
    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realSize);
    buf->size += realSize;
    buf->data[buf->size] = '\0';
    return realSize;
}

char* rfFetch(const char* url, const char* method, const char* body) {
    CURL* curl = curl_easy_init();
    if (!curl) return NULL;

    struct FetchBuffer buf = { .data = malloc(1), .size = 0 };
    buf.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "prism-lang");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    if (method && strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode >= 400) {
        free(buf.data);
        return NULL;
    }

    return buf.data;
}

char* resolvePackage(const char* name) {
    char dir[512];
    char cacheDir[1024];
    char cachePath[2048];

    getRedfieldDir(dir, sizeof(dir));
    snprintf(cacheDir, sizeof(cacheDir), "%s/packages", dir);
    snprintf(cachePath, sizeof(cachePath), "%s/%s.pr", cacheDir, name);

    if (fileExists(cachePath)) {
        fprintf(stderr, "DEBUG: found in cache\n");
        return strdup(cachePath);
    }

    fprintf(stderr, "DEBUG: fetching from registry\n");
    char url[512];
    snprintf(url, sizeof(url),
        "https://raw.githubusercontent.com/%s/prism-packages/main/%s/init.pr",
        name, name);

    char* result = rfFetch(url, "GET", NULL);
    if (!result) {
        fprintf(stderr, "ERROR: failed to fetch package '%s'\n", name);
        return NULL;
    }

    mkdirRecursive(cacheDir);
    saveToFile(cachePath, result);
    free(result);
    return strdup(cachePath);
}