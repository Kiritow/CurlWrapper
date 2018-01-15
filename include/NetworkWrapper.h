#pragma once
#include <string>
#include <functional>

class HTTPConnection
{
public:
    HTTPConnection();
    ~HTTPConnection();

    bool isReady() const;

    /// Behavior Options
    int setVerbos(bool v);/// disabled by default.
    int setHeaderInBody(bool enable);/// disabled by default.

    int setURL(const std::string& URL);
    int setHeaderWriter(const std::function<int(char*,int)>& fn);
    int setHeaderOutputFile(const std::string& filename);
    int setDataWriter(const std::function<int(char*,int)>& fn);
    int setDataOutputFile(const std::string& filename);
    int setDataReader(const std::function<int(char*,int)>& fn);
    int setDataInputFile(const std::string& filename);
    int setTimeout(int second);

    /// HTTP Option
    int setAcceptEncoding(const std::string& encoding);
    int setAcceptEncodingAll();
    int setTransferEncoding(bool enable);/// disabled by default

    enum class Method
    {
        Get,Post
    };

    int setMethod(Method m);/// Method::Get by default.

    int perform();

    int getLastErrCode();
    std::string getLastError();
private:
    class _impl;
    _impl* _p;
};
