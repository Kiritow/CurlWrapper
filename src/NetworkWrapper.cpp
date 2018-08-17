#include "NetworkWrapper.h"
#include "curl/curl.h"
#include <vector>
#include <sstream>
#include <memory>
#include <cstdio>
#include <list>
using namespace std;

/// Use C++11 Standard replacement-list marco.
#ifdef NETWORKE_WRAPPER_DEBUG
static string _internal_libcurl_getfuncname(void* libfnptr);
#define invokeLib(LibFn,CurlPtr,...) [&,this](const string& _excall_name){ \
    debug_info _libcall_info; \
    _libcall_info.libfn=_internal_libcurl_getfuncname((void*)LibFn); \
    _libcall_info.callfn=_excall_name; \
    _p->lasterr=LibFn(CurlPtr,##__VA_ARGS__); \
    _libcall_info.ret=_p->lasterr; \
    _libcall_info.callid=_p->_libnextcall_id++; \
    _p->_libcall.push_back(_libcall_info); \
    if(_p->_libcall_max>0 && (int)_p->_libcall.size()>_p->_libcall_max) \
    { \
        _p->_libcall.pop_front(); \
    } \
    return _p->lasterr; \
}(__func__)
#else
#define invokeLib(LibFn,CurlPtr,...) _p->lasterr=LibFn(CurlPtr,##__VA_ARGS__)
#endif

#ifdef _MSC_VER
#ifdef _CRT_SECURE_NO_WARNINGS
#pragma message("[WARNING] Stop C runtime secure check may cause bugs.")
#else
/// Stop SDL check errors in VS. (Even if SDL check is disabled...)
FILE* _fopen_bridge(const char* filename, const char* mode)
{
	FILE* fp = NULL;
	if (fopen_s(&fp, filename, mode) != 0)
	{
		return NULL;
	}
	else
	{
		return fp;
	}
}
/// Replace deprecated functions
#define fopen _fopen_bridge

#endif
#endif

class _libcurl_native_init_class
{
public:
    _libcurl_native_init_class()
    {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    ~_libcurl_native_init_class()
    {
        curl_global_cleanup();
    }

    static _libcurl_native_init_class& get()
    {
        static _libcurl_native_init_class _obj;
        return _obj;
    }
};

/// Control block used by internal buffered writer
class _buffered_data_writer_control_block
{
public:
    /// Not using smart pointer here. maybe a issue...
    void* ptr;
    int used;
    int maxsz;
    bool canextend;

    _buffered_data_writer_control_block()
    {
        ptr=nullptr;
        used=0;
        maxsz=0;
        canextend=false;
    }

    bool extendTo(int newMaxsz)
    {
        if(!canextend)
        {
            /// forbidden
            return false;
        }
        if(newMaxsz<=maxsz)
        {
            /// Does not need extend...
            return true;
        }
        void* nptr=malloc(newMaxsz);
        if(!nptr)
        {
            fprintf(stderr,"%s(%p): Failed calling extendTo. Out of memory.\n",__func__,this);
            return false; /// out of memory
        }
        else
        {
            fprintf(stderr,"%s(%p): extend buffer to size %d\n",__func__,this,newMaxsz);
        }

        memset(nptr,0,newMaxsz);

        if(ptr)
        {
            memcpy(nptr,ptr,used);
            free(ptr);
        }

        ptr=nptr;
        maxsz=newMaxsz;
        return true;
    }

    ~_buffered_data_writer_control_block()
    {
        if(canextend) /// the ptr is extended.
        {
            free(ptr);
            ptr=nullptr;
        }
    }
};

class HTTPConnection::_impl
{
public:
    CURL* c;
    CURLcode lasterr;
    vector<FILE*> delegated_fp;

    /// Internal control blocks
    shared_ptr<_buffered_data_writer_control_block> spcHeader,spcData;

    #ifdef NETWORKE_WRAPPER_DEBUG
    /// Debug API Variable
    list<debug_info> _libcall;
    int _libcall_max;
    int _libnextcall_id;
    #endif
};

HTTPConnection::HTTPConnection() : _p(new _impl)
{
    _p->c=NULL;
    _p->lasterr=CURLE_OK;

    _p->c=curl_easy_init();

    #ifdef NETWORKE_WRAPPER_DEBUG
    /// DebugAPI Initialization
    _p->_libcall_max=20;
    _p->_libnextcall_id=0;
    #endif
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

/// Behavior Options
int HTTPConnection::setVerbos(bool v)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_VERBOSE,v?1:0);
}

int HTTPConnection::setErrStream(FILE* stream)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_STDERR,stream);
}

int HTTPConnection::setSSLVerifyPeer(bool enable)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_SSL_VERIFYPEER,enable?1:0);
}

int HTTPConnection::setSSLVerifyHost(bool enable)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_SSL_VERIFYHOST,enable?2:0);
}

int HTTPConnection::setHeaderInBody(bool enable)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADER,enable?1:0);
}

int HTTPConnection::setSignal(bool hasSignal)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_NOSIGNAL,hasSignal?0:1);
}

int HTTPConnection::enableProgress(bool hasProgress)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_NOPROGRESS,hasProgress?0:1);
}

static int _general_progress_callback(void* userfn,curl_off_t dltotal,curl_off_t dlnow,curl_off_t ultotal,curl_off_t ulnow)
{
    return (*reinterpret_cast<function<int(long long,long long,long long,long long)>*>(userfn))(dltotal,dlnow,ultotal,ulnow);
}

int HTTPConnection::setProgressMeter(const function<int(long long, long long, long long, long long)>& fn)
{
    enableProgress(true);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_XFERINFOFUNCTION,_general_progress_callback);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_XFERINFODATA,&fn);
    return 0;
}

/// Data Functions
static size_t _general_data_callback(char* ptr,size_t sz,size_t n,void* userfn)
{
    int sum=sz*n;
    return (*reinterpret_cast<function<int(char*,int)>*>(userfn))(ptr,sum);
}

static size_t _buffered_data_writer_callback(char* ptr,size_t sz,size_t n,void* pblock)
{
    int sum=sz*n;
    _buffered_data_writer_control_block* p=(_buffered_data_writer_control_block*)pblock;
    if(p->used<p->maxsz)
    {
        /// Ignore n here? or copied an incomplete one?
        int realcopy=min(sum,p->maxsz-p->used);
        memcpy((char*)p->ptr+p->used,ptr,realcopy);
        p->used+=realcopy;
    }
    return n;/// follow fwrite return result. But what if realcopy==0? Still follow this?
}

static size_t _buffered_data_writer_resize_callback(char* ptr,size_t sz,size_t n,void* pblock)
{
    int sum=sz*n;
    _buffered_data_writer_control_block* p=(_buffered_data_writer_control_block*)pblock;
    if(p->maxsz-p->used<sum)
    {
        if(!p->extendTo(p->maxsz+sum+8))
        {
            fprintf(stderr,"%s: Failed on calling extendTo with %p.\n",__func__,p);
            return n; /// follow fwrite return result. But what will the data goes...?
        }
    }
    /// memory is ok now
    memcpy((char*)p->ptr+p->used,ptr,sum);
    p->used+=sum;
    return n;
}

int HTTPConnection::setDataWriter(const function<int(char*,int)>& fn)
{
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_WRITEFUNCTION,_general_data_callback);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_WRITEDATA,&fn);
    return 0;
}

int HTTPConnection::setHeaderWriter(const function<int(char*, int)>& fn)
{
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADERFUNCTION,_general_data_callback);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADERDATA,&fn);
    return 0;
}

int HTTPConnection::setDataReader(const function<int(char*, int)>& fn)
{
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_READFUNCTION,_general_data_callback);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_READDATA,&fn);
    return 0;
}

int HTTPConnection::setHeaderOutputBuffer(void* ptr, int maxsz)
{
    _p->spcHeader.reset(new _buffered_data_writer_control_block);
    if(!_p->spcHeader.get())
    {
        return -2; /// not enough memory
    }

    if(ptr==nullptr) /// use internal extended buffer.
    {
        _p->spcHeader->canextend=true;
        invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADERFUNCTION,_buffered_data_writer_resize_callback);
    }
    else
    {
        memset(ptr,0,maxsz);
        _p->spcHeader->ptr=ptr;
        _p->spcHeader->maxsz=maxsz;
        _p->spcHeader->canextend=false;
        invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADERFUNCTION,_buffered_data_writer_callback);
    }

    invokeLib(curl_easy_setopt,_p->c,CURLOPT_HEADERDATA,_p->spcHeader.get());

    return 0;
}

int HTTPConnection::setDataOutputBuffer(void* ptr, int maxsz)
{
    _p->spcData.reset(new _buffered_data_writer_control_block);
    if(!_p->spcData.get())
    {
        return -2; /// not enough memory
    }

    if(ptr==nullptr) /// use internal extended buffer.
    {
        _p->spcData->canextend=true;
        invokeLib(curl_easy_setopt,_p->c,CURLOPT_WRITEFUNCTION,_buffered_data_writer_resize_callback);
    }
    else
    {
        memset(ptr,0,maxsz);
        _p->spcData->ptr=ptr;
        _p->spcData->maxsz=maxsz;
        _p->spcData->canextend=false;
        invokeLib(curl_easy_setopt,_p->c,CURLOPT_WRITEFUNCTION,_buffered_data_writer_callback);
    }

    invokeLib(curl_easy_setopt,_p->c,CURLOPT_WRITEDATA,_p->spcData.get());

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

int HTTPConnection::enableCookieEngine()
{
    return setCookieInputFile("");/// follow libcurl official document, this can enable the cookie engine with no side effects.
}

int HTTPConnection::setCookieInputFile(const std::string& filename)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_COOKIEFILE,filename.c_str());
}

int HTTPConnection::setCookieOutputFile(const std::string& filename)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_COOKIEJAR,filename.c_str());
}

int HTTPConnection::setCookieSession(bool new_session)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_COOKIESESSION,new_session?1:0);
}

int HTTPConnection::addCookie(const std::string& raw_cookie)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_COOKIELIST,raw_cookie.c_str());
}

int HTTPConnection::clearCookie()
{
    return addCookie("ALL");
}

int HTTPConnection::clearSessionCookie()
{
    return addCookie("SESS");
}

int HTTPConnection::flushCookie()
{
    return addCookie("FLUSH");
}

int HTTPConnection::reloadCookie()
{
    return addCookie("RELOAD");
}

int HTTPConnection::setTimeout(int second)
{
    return curl_easy_setopt(_p->c,CURLOPT_TIMEOUT,second);
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

int HTTPConnection::setUserAgent(const std::string& user_agent)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_USERAGENT,user_agent.c_str());
}

int HTTPConnection::setReferer(const std::string& referer)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_REFERER,referer.c_str());
}

int HTTPConnection::setOrigin(const std::string& origin)
{
    curl_slist* lst=NULL;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_COOKIELIST,&lst);
    string s("Origin: ");
    s.append(origin);
    curl_slist_append(lst,s.c_str());

    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_HTTPHEADER,lst);
}

int HTTPConnection::setPostData(const void* data,int sz)
{
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_POSTFIELDSIZE,sz);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_COPYPOSTFIELDS,data);
    return 0;
}

int HTTPConnection::setPostData(const std::string& data)
{
    return setPostData(data.c_str(),data.size());
}

int HTTPConnection::setFollowLocation(bool enable)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_FOLLOWLOCATION,enable?1:0);
}

int HTTPConnection::setMethod(Method m)
{
    switch(m)
    {
    case Method::Get:
        return invokeLib(curl_easy_setopt,_p->c,CURLOPT_HTTPGET,1);
    case Method::Post:
        return invokeLib(curl_easy_setopt,_p->c,CURLOPT_POST,1);
    default:
        return -2;
    }
}

int HTTPConnection::setURL(const string& URL)
{
	return invokeLib(curl_easy_setopt, _p->c, CURLOPT_URL, URL.c_str());
}

std::string HTTPConnection::escape(const std::string& rawURL)
{
	char* p = curl_easy_escape(_p->c, rawURL.c_str(), rawURL.size());
	std::string s(p);
	curl_free(p);
	return s;
}

std::string HTTPConnection::unescape(const std::string& URL)
{
	int len = 0;
	char* p = curl_easy_unescape(_p->c, URL.c_str(), URL.size(), &len);
	std::string s(p, len);
	curl_free(p);
	return s;
}

int HTTPConnection::setKeepAlive(long idle_second, long interval_second)
{
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_TCP_KEEPALIVE,1);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_TCP_KEEPIDLE,idle_second);
    invokeLib(curl_easy_setopt,_p->c,CURLOPT_TCP_KEEPINTVL,interval_second);
    return 0;
}

int HTTPConnection::disableKeepAlive()
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_TCP_KEEPALIVE,0);
}

int HTTPConnection::setProxyType(const ProxyType& type)
{
	switch (type)
	{
	case ProxyType::Http:
		return invokeLib(curl_easy_setopt, _p->c, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
	case ProxyType::Https:
		return invokeLib(curl_easy_setopt, _p->c, CURLOPT_PROXYTYPE, CURLPROXY_HTTPS);
	case ProxyType::Http1_0:
		return invokeLib(curl_easy_setopt, _p->c, CURLOPT_PROXYTYPE, CURLPROXY_HTTP_1_0);
	case ProxyType::Socks4:
		return invokeLib(curl_easy_setopt, _p->c, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
	case ProxyType::Socks4a:
		return invokeLib(curl_easy_setopt, _p->c, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4A);
	case ProxyType::Socks5:
		return invokeLib(curl_easy_setopt, _p->c, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
	case ProxyType::Socks5_Hostname:
		return invokeLib(curl_easy_setopt, _p->c, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
	default:
		return -2;
	}
}

int HTTPConnection::setProxy(const std::string & proxy)
{
	return invokeLib(curl_easy_setopt, _p->c, CURLOPT_PROXY, proxy.c_str());
}

int HTTPConnection::setPreProxy(const std::string & preproxy)
{
	return invokeLib(curl_easy_setopt, _p->c, CURLOPT_PRE_PROXY, preproxy.c_str());
}

int HTTPConnection::perform()
{
    return invokeLib(curl_easy_perform,_p->c);
}

int HTTPConnection::getResponseCode()
{
    long code;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_RESPONSE_CODE,&code);
    return code;
}

vector<Cookie> HTTPConnection::getCookies()
{
    curl_slist* lst=NULL;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_COOKIELIST,&lst);
    vector<Cookie> vec;
    while(lst)
    {
        string data=lst->data;
        Cookie c;
        string tmp;

        istringstream istr(data);
        istr>>c.domain;
        istr>>tmp;
        c.flag=tmp=="TRUE";
        istr>>c.path;
        istr>>tmp;
        c.secure=tmp=="TRUE";
        istr>>c.expiration>>c.name>>c.value;

        vec.push_back(c);

        lst=lst->next;
    }
    curl_slist_free_all(lst);
    return vec;
}

const void* HTTPConnection::getHeaderOutputBuffer()
{
    return _p->spcHeader->ptr;
}

const void* HTTPConnection::getDataOutputBuffer()
{
    return _p->spcData->ptr;
}

const int HTTPConnection::getHeaderOutputBufferLength()
{
    return _p->spcHeader->used;
}

const int HTTPConnection::getDataOutputBufferLength()
{
    return _p->spcData->used;
}

string HTTPConnection::getRedirectURL()
{
    char* url=NULL;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_REDIRECT_URL,&url);
    if(url)
    {
        return url;
    }
    else
    {
        return "";
    }
}

int HTTPConnection::getRedirectCount()
{
    long cnt=0;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_REDIRECT_COUNT,&cnt);
    return cnt;
}

string HTTPConnection::getContentType()
{
    char* ct=NULL;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_CONTENT_TYPE,&ct);
    if(ct)
    {
        return string(ct);
    }
    else
    {
        return "";
    }
}

int HTTPConnection::getContentLengthDownload()
{
    curl_off_t sz;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,&sz);
    return sz;
}

int HTTPConnection::getContentLengthUpload()
{
    curl_off_t sz;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_CONTENT_LENGTH_UPLOAD_T,&sz);
    return sz;
}

double HTTPConnection::getNameLookUpTime()
{
    double t=0;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_NAMELOOKUP_TIME,&t);
    return t;
}

double HTTPConnection::getConnectTime()
{
    double t=0;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_CONNECT_TIME,&t);
    return t;
}

double HTTPConnection::getAppConnectTime()
{
    double t=0;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_APPCONNECT_TIME,&t);
    return t;
}

double HTTPConnection::getPretransferTime()
{
    double t=0;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_PRETRANSFER_TIME,&t);
    return t;
}

double HTTPConnection::getStartTransferTime()
{
    double t=0;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_STARTTRANSFER_TIME,&t);
    return t;
}

double HTTPConnection::getTotalTime()
{
    double t=0;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_TOTAL_TIME,&t);
    return t;
}

double HTTPConnection::getRedirectTime()
{
    double t=0;
    invokeLib(curl_easy_getinfo,_p->c,CURLINFO_REDIRECT_TIME,&t);
    return t;
}

int HTTPConnection::getLastErrCode()
{
    return _p->lasterr;
}

string HTTPConnection::getLastError()
{
    return curl_easy_strerror(_p->lasterr);
}

#ifdef NETWORKE_WRAPPER_DEBUG
/// NetworkWrapper Debug API
int HTTPConnection::setDebugQueueLength(int length)
{
    _p->_libcall_max=length;
    return 0;
}

int HTTPConnection::traversalDebugQueue(const function<int(const debug_info&)>& fn)
{
    int cnt=0;
    for(const auto& info:_p->_libcall)
    {
        ++cnt;
        if(fn(info)!=0)
        {
            return cnt;
        }
    }
    return cnt;
}

#define DECLFN(LibFn) if(libfnptr==(void*)LibFn) { return #LibFn ; } else
static string _internal_libcurl_getfuncname(void* libfnptr)
{
    DECLFN(curl_easy_setopt);
    DECLFN(curl_easy_perform);

    return "(Unknown Function)";
}
#undef DECLFN
#else
/// NetworkWrapper Debug API (no-op)
int HTTPConnection::setDebugQueueLength(int length)
{
    return 0;
}
int HTTPConnection::traversalDebugQueue(const std::function<int(const debug_info&)>& fn)
{
    return 0;
}
#endif

/// Undefine
#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#undef fopen
#endif
#endif
