#pragma once
#include <string>
#include <vector>
#include <functional>

class Cookie
{
public:
    std::string domain;
    bool flag;
    std::string path;
    bool secure;
    int expiration;
    std::string name;
    std::string value;
};

class HTTPConnection
{
public:
    HTTPConnection();
    ~HTTPConnection();

    bool isReady() const;

    /// Behavior Options
    int setVerbos(bool v);/// disabled by default.
    int setSSLVerifyPeer(bool enable);/// enabled by default.
    int setSSLVerifyHost(bool enable);/// enabled by default.

    int setHeaderInBody(bool enable);/// disabled by default.

    int setURL(const std::string& URL);

    /// If set...OutputBuffer() is called, then an internal data writer function will be used to fill it.
    /// So don't use set...OutputBuffer() and set...Writer() together.
    /// If ptr is nullptr and maxsz is not 0, then a new buffer will be created with given size to contain the data. Return -2 on not enough memory.
    /// If ptr is nullptr and maxsz is 0, then the buffer will be created after received data size is calculated. If there's not enough memory, get...OutputBuffer() will return nullptr.
    /// get...OutputBuffer() will return internal buffer if used. DO NOT call free or delete on it.
    int setHeaderWriter(const std::function<int(char*,int)>& fn);
    int setHeaderOutputBuffer(void* ptr,int maxsz);
    int setHeaderOutputFile(const std::string& filename);

    int setDataWriter(const std::function<int(char*,int)>& fn);
    int setDataOutputBuffer(void* ptr,int maxsz);
    int setDataOutputFile(const std::string& filename);

    int setDataReader(const std::function<int(char*,int)>& fn);
    int setDataInputFile(const std::string& filename);

    int enableCookieEngine();
    int setCookieInputFile(const std::string& filename);
    int setCookieOutputFile(const std::string& filename);
    int setCookieSession(bool new_session);

    int addCookie(const std::string& raw_cookie);
    int clearCookie();
    int clearSessionCookie();
    int flushCookie();
    int reloadCookie();

    int setTimeout(int second);

    /// HTTP Option
    int setAcceptEncoding(const std::string& encoding);
    int setAcceptEncodingAll();
    int setTransferEncoding(bool enable);/// disabled by default
    int setUserAgent(const std::string& user_agent);
    int setReferer(const std::string& referer);
    int setOrigin(const std::string& origin);
    int setPostData(const void* data,int sz);
    int setPostData(const std::string& data);
    int setFollowLocation(bool enable);/// disabled by default

    enum class Method
    {
        Get,Post
    };

    int setMethod(Method m);/// Method::Get by default.

    int perform();

    /// Response
    int getResponseCode();
    std::vector<Cookie> getCookies();
    const void* getHeaderOutputBuffer();
    const void* getDataOutputBuffer();

    /// Error handling
    int getLastErrCode();
    std::string getLastError();
private:
    class _impl;
    _impl* _p;
};
