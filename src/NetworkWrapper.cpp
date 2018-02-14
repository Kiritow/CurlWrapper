#include "NetworkWrapper.h"
#include "curl/curl.h"
#include <vector>
#include <sstream>
#include <memory>
#include <cstdio>
using namespace std;

/// Use C++11 Standard replacement-list marco.
#define invokeLib(LibFn,CurlPtr,...) _p->lasterr=LibFn(CurlPtr,##__VA_ARGS__)

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

int HTTPConnection::setVerbos(bool v)
{
    return invokeLib(curl_easy_setopt,_p->c,CURLOPT_VERBOSE,v?1:0);
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

int HTTPConnection::getLastErrCode()
{
    return _p->lasterr;
}

string HTTPConnection::getLastError()
{
    return curl_easy_strerror(_p->lasterr);
}
