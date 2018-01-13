#include "NetworkWrapper.h"
#include "curl/curl.h"
#include <vector>
using namespace std;

#define invokeLib(LibFn,arg1,args...) _p->lasterr=LibFn(arg1,##args)

static int _cnt_native_lib=0;

int InitNativeLib()
{
    if(_cnt_native_lib==0 && curl_global_init(CURL_GLOBAL_ALL)!=0)
    {
        return -1;
    }
    else
    {
        _cnt_native_lib++;
        return 0;
    }
}

int CleanUpNativeLib()
{
    if(_cnt_native_lib==1)
    {
        curl_global_cleanup();
        _cnt_native_lib=0;
        return 0;
    }
    else
    {
        _cnt_native_lib--;
        return 0;
    }
}

class HTTPConnection::_impl
{
public:
    CURL* c;
    CURLcode lasterr;
    vector<FILE*> delegated_fp;
};

HTTPConnection::HTTPConnection() : _p(new _impl)
{
    _p->c=NULL;
    _p->lasterr=CURLE_OK;

    _p->c=curl_easy_init();
}

HTTPConnection::~HTTPConnection()
{
    if(_p)
    {
        curl_easy_cleanup(_p->c);

        for(auto& ptr:_p->delegated_fp)
        {
            fclose(ptr);
        }

        delete _p;
    }
}

bool HTTPConnection::isReady() const
{
    return _p&&_p->c;
}

int HTTPConnection::setURL(const string& URL)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_URL,URL.c_str());
}

static size_t _general_writer(char* ptr,size_t sz,size_t n,void* userfn)
{
    int sum=sz*n;
    return (*reinterpret_cast<function<int(char*,int)>*>(userfn))(ptr,sum);
}

int HTTPConnection::setDataWriter(const function<int(char*,int)>& fn)
{
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_WRITEFUNCTION,_general_writer);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_WRITEDATA,&fn);
    return 0;
}

int HTTPConnection::setHeaderWriter(const std::function<int(char*, int)>& fn)
{
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADERFUNCTION,_general_writer);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADERDATA,&fn);
    return 0;
}

int HTTPConnection::setDataOutputFile(const string& filename)
{
    FILE* fp=fopen(filename.c_str(),"wb");
    if(!fp) return -2;

    invokeLib(curl_easy_setopt,_p->c,CURLOPT_WRITEFUNCTION,fwrite);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_WRITEDATA,fp);

    _p->delegated_fp.push_back(fp);

    return 0;
}

int HTTPConnection::setHeaderOutputFile(const string& filename)
{
    FILE* fp=fopen(filename.c_str(),"wb");
    if(!fp) return -2;

    invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADERFUNCTION,fwrite);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADERDATA,fp);

    _p->delegated_fp.push_back(fp);
    return 0;
}

int HTTPConnection::setDataInputFile(const std::string& filename)
{
    FILE* fp=fopen(filename.c_str(),"rb");
    if(!fp) return -2;

    invokeLib(curl_easy_setopt,_p->c,CURLOPT_READFUNCTION,fread);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_READDATA,fp);

    _p->delegated_fp.push_back(fp);
    return 0;
}

int HTTPConnection::setTimeout(int second)
{
    return curl_easy_setopt(_p->c,CURLOPT_TIMEOUT,second);
}

int HTTPConnection::setVerbos(bool v)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_VERBOSE,v?1:0);
}

int HTTPConnection::setHeaderInBody(bool enable)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADER,enable?1:0);
}

int HTTPConnection::setAcceptEncoding(const std::string& encoding)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_ACCEPT_ENCODING,encoding.c_str());
}

int HTTPConnection::setAcceptEncodingAll()
{
    return setAcceptEncoding("");
}

int HTTPConnection::setTransferEncoding(bool enable)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_TRANSFER_ENCODING,enable?1L:0);
}

int HTTPConnection::perform()
{
    return invokeLib(curl_easy_perform,_p->c);
}

int HTTPConnection::getLastErrCode()
{
    return _p->lasterr;
}

string HTTPConnection::getLastError()
{
    return curl_easy_strerror(_p->lasterr);
}

