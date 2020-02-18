#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include <errno.h>
#include <unistd.h>

#include "cdata.h"
#include "vovu.h"
#include "log.h"
#include "vcomms.h"
#include "cio.h"

#define REQ_NULL    0
#define REQ_GET     1
#define REQ_OPTIONS 2
#define REQ_HEAD    3
#define REQ_UNSUPP  8
#define REQ_UNKNOWN 9

#define URL_NO      0
#define URL_YES     1
#define URL_ALL     2

#define HTTP_BAD    0
#define HTTP_11     1
#define HTTP_10     2

#define HTTP_C_COUNT 8

static const char http_c[][10] =
    {
        "GET ",       //0
        "OPTIONS ",   //1
        "HEAD ",      //2
        "POST ",      //3
        "PUT ",       //4
        "DELETE ",    //5
        "TRACE ",     //6
        "CONNECT "    //7
    };

static const int http_i[] =
    {
        REQ_GET,      //0
        REQ_OPTIONS,  //1
        REQ_HEAD,     //2
        REQ_UNSUPP,   //3
        REQ_UNSUPP,   //4
        REQ_UNSUPP,   //5
        REQ_UNSUPP,   //6
        REQ_UNSUPP    //7
    };

char *notfound_html = "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\"><html><body>Not Found</body></html>";
char *badrequest_html = "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\"><html><body>Bad Request</body></html>";

static const char wday_name[][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"  };
static const char mon_name[][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

char *par;
char *subs_par;

struct vissy_settings *pvisset;
struct vissy_stats    *pvisstat;

void vcomms_init(struct vissy_settings *se,struct vissy_stats *st)
{
    pvisset = se;
    pvisstat = st;
    par = (char *)malloc(sizeof(char) * MAX_READ_SIZE);
    subs_par = (char *)malloc(sizeof(char) * MAX_READ_SIZE);
}

void create_time_line(char *buffer)
{
    time_t rawtime;
    struct tm * ptm;

    time(&rawtime);
    ptm = gmtime (&rawtime);
    //Date: Thu, 15 Jan 2015 10:39:18 GMT
    snprintf(buffer,64,"Date: %.3s, %d %.3s %d %.2d:%.2d:%.2d GMT",
                    wday_name[ptm->tm_wday],
                    ptm->tm_mday,
                    mon_name[ptm->tm_mon],
                    1900 + ptm->tm_year,
                    ptm->tm_hour,
                    ptm->tm_min,
                    ptm->tm_sec);
}


int vcomms_received(struct CliConn *client,char *message,const char *url_to_handle)
{

    int keepalive = 0;
    int http      = HTTP_BAD;
    int urlmatch  = URL_NO;
    int reqtype   = REQ_UNKNOWN;

    char *l;

    //An established client send connection request.
    //(Probably because the bogous proxy or redirection)
    //If the reinit option enabled, we reset the connection and accept reconnect.
    if(client->status == STATUS_COMM)
    {
        if(get_reinit_allowed() && startWithStr(message,"GET "))
        {
            toLog(0,"Re-Initialize client <%d> ...\n",client->descr);
            ++(pvisstat->allreinit);
            ++client->reinit;
            client->status = STATUS_NEW;
            client_subscribe_clear(client);
            client->err = 0;
            client->created = time(NULL);
        }
        else
        {
            toLog(0,"Connected client message <%d>: %s\n",client->descr,message);
            return 0;
        }
    }

    reqtype = REQ_NULL;
    l = strtok(message,"\n");
    while(l != NULL)
    {
        chop(l);
        printf("*** %s\n",l);

        if(emptyStr(l))
        {
            l = strtok(NULL,"\n");
            continue;
        }

        if(reqtype == REQ_NULL)
        {
            int i;
            reqtype = REQ_UNKNOWN;
            for(i=0;i<HTTP_C_COUNT;++i)
            {
                if(startWithStr(l,http_c[i]))
                {
                    reqtype = http_i[i];
                    l=nextNotWhitespace(l);
                    client->status = STATUS_HEADER;
                    break;
                }
            }

            char *httpsign;
            if((httpsign=strstr(l," HTTP/1.1")) != NULL)
            {
                http = HTTP_11;
                *httpsign = '\0';
            }
            else if((httpsign=strstr(l," HTTP/1.0")) != NULL)
            {
                http = HTTP_10;
                *httpsign = '\0';
            }
            else if((httpsign=strstr(l," HTTP/")) != NULL)
            {
                http = HTTP_BAD;
                *httpsign = '\0';
            }

            //check the requested url
            if(!strncmp(l,url_to_handle,strlen(url_to_handle)))
            {
                char *nextchar = l+strlen(url_to_handle);

                if(isspace(*nextchar) || *nextchar == '\0')
                    urlmatch = URL_YES;
                if(*nextchar == '?')
                {
                    urlmatch = URL_YES;
                    strncpy(par,nextchar+1,MAX_READ_SIZE-1);
                }
            }
            if(!strncmp(l,"* ",2))
            {
                urlmatch = URL_ALL;
            }
        }

        if(client->status == STATUS_HEADER)
        {
            if(!strncmp(l,"Connection: keep-alive",22))
                keepalive = 1;
            if(!strncmp(l,"Connection: Keep-Alive",22))
                keepalive = 1;
            if(startWithStr(l,"User-Agent:"))
                strncpy(client->agent,nextNotWhitespace(l),190);
        }

        l = strtok(NULL,"\n");
    }

    //End reading header, do what we need to do...
    if(reqtype == REQ_GET)
    {
        if(urlmatch != URL_YES)
        {
            toLog(0,"Client request unknown URL, Closing <%d>...\n",client->descr);
            vcomms_send_notfound(client);
            close_client(client->descr);
            return 0;
        }
        if(http != HTTP_11)
        {
            toLog(0,"Client request not HTTP/1.1 protocol, Closing <%d>...\n",client->descr);
            vcomms_send_notsupported(client);
            close_client(client->descr);
            return 0;
        }
        if(!keepalive)
        {
            toLog(0,"Client not added keep-alive, Closing <%d>...\n",client->descr);
            vcomms_send_badreq(client);
            close_client(client->descr);
            return 0;
        }

        toLog(0,"SSE request (parameter:%s)\n",par);
        if(vcomms_parseparam(client,par) == 0)
        {
            vcomms_send_handshake(client);
            toLog(0,"SSE connection: OK <%d>>>>>>>>>>>> start data stream\n",client->descr);
            ++(pvisstat->allclient);
        }
        else
        {
            toLog(0,"Error in GET parameters. <%d> (Missing or wrong \"subscribe\" parameter)\n"
                    "Closing connection...\n"
                        ,client->descr);
            vcomms_send_badreq(client);
            close_client(client->descr);
        }
        return 0;
    }

    if(reqtype == REQ_HEAD)
    {
        if(urlmatch != URL_YES)
        {
            toLog(0,"Client request unknown URL, Closing <%d>...\n",client->descr);
            vcomms_send_notfound(client);
            close_client(client->descr);
            return 0;
        }
        if(http != HTTP_11) 
        {
            toLog(0,"Client request not HTTP/1.1 protocol, Closing <%d>...\n",client->descr);
            vcomms_send_notsupported(client);
            close_client(client->descr);
            return 0;
        }

        toLog(0,"Head requested. Send header and close <%d>...\n",client->descr);
        vcomms_send_head(client);
        close_client(client->descr);
        return 0;
    }

    if(reqtype == REQ_OPTIONS)
    {
        if(urlmatch != URL_YES && urlmatch != URL_ALL)
        {
            toLog(0,"Client request unknown URL, Closing <%d>...\n",client->descr);
            vcomms_send_notfound(client);
            close_client(client->descr);
            return 0;
        }
        if(http != HTTP_11) 
        {
            toLog(0,"Client request not HTTP/1.1 protocol, Closing <%d>...\n",client->descr);
            vcomms_send_notsupported(client);
            close_client(client->descr);
            return 0;
        }

        toLog(0,"Options requested. Send header and close <%d>...\n",client->descr);
        vcomms_send_options(client);
        close_client(client->descr);
        return 0;
    }

    toLog(0,"Not supported command received, Closing <%d>...\n",client->descr);
    vcomms_send_notsupported(client);
    close_client(client->descr);
    return 0;
}

int vcomms_parseparam(struct CliConn *client,char *parameters)
{

    int i;
    char *p,*st;

    char id_par[69];

    char *param;
    strcpy(subs_par,"");
    strcpy(id_par,"");
    param = strtok(parameters,"&");
    while(param != NULL)
    {
        if(strncmp(param,"subscribe=",10) == 0)
            strncpy(subs_par,param,MAX_READ_SIZE-1);
        if(strncmp(param,"id=",3) == 0)
            strncpy(id_par,param,68);
        param = strtok(NULL,"&");
    }

    //Examine id parameter
    if(strcmp(id_par,"") != 0)
    {
        p = id_par + 3;
        for(i=0;p[i] != '\0' && p[i] != '&' && p[i] != '\n' && p[i] != '\r' && i < 64;++i);
        p[i] = '\0';
        strncpy(client->uniq_id,p,62);
    }

    //Examine subscribe parameter
    if(strcmp(subs_par,"") != 0)
    {
        p = subs_par + 10;
        for(i=0;p[i] != '\0' && p[i] != '&' && p[i] != '\n' && p[i] != '\r';++i);
        p[i] = '\0';
        st = strtok(p,"-");
        while(st != NULL)
        {
            printf(" + subscribe to: %s\n",st);
            client_subscribe_add(client,st);
            st = strtok(NULL,"-");
        }
        return 0;
    }

    return 1;
}

#define SSE_PAYLOADMAX   6*1024
char send_outbuffer[SSE_PAYLOADMAX];
int sendmessages(char *buf)
{
    int mm;
    int mr;
    char error_o=0;
    char *t;
    char token[32];
    char rejectId[64];

    strcpy(rejectId,"");
    t = strtok(buf,"=");
    if(t == NULL || strlen(t) > 32)
    {
        printf("%s\n",buf);
        toLog(1,"Wrong formatted message from communication channel (1), ignored.\n");
        return 1;
    }
    strncpy(token,t,32);

    t = strtok(NULL,"=");

    if(t == NULL)
    {
        toLog(1,"Wrong formatted message (null) from communication channel (2), ignored.\n");
        return 1;
    }

    ++(pvisstat->allmessage);

    mm = get_pos(token,'*');
    if(mm == -1)
        mm = 32;

    mr = get_pos(token,'-');
    if(mr != -1)
    {
        if(mm!=32 && mr<mm)
        {
            toLog(0,"Wrong formatted message from communication channel (3), ignored.\n");
            return 1;
        }
        strncpy(rejectId,token+mr+1,62);
        token[mr] = '\0';
    }

    client_start();
    while(client_next() != NULL)
    {
        if (client_current()->status == STATUS_COMM)
        {
            if(client_subscribe_exists(client_current(),token,mm,rejectId))
            {
                strcpy(send_outbuffer, "");
                client_current()->message++;
                ++(pvisstat->allsmessage);

                vcomms_payload_encode(send_outbuffer,t);
                if(cio_high_write(client_current(),send_outbuffer))
                {
                    error_o = 1;
                    toLog(2,"%s\n",send_outbuffer);
                }

            }
        }
    }

    if(error_o)
    {
        client_start();
        while(client_next() != NULL)
            while(client_current()->err)
            {
                toLog(0,"Error on send, closing client %s:\n",client_current()->uniq_id);
                // need to resolve rather than terminate???
                close_client(client_current()->descr);
            }
    }

    return 0;
}

char vcomms_chunk_encoding_idbuffer[64];

int vcomms_payload_encode(char *outbuffer,char *message)
{
    chop(message);
    snprintf(vcomms_chunk_encoding_idbuffer,60,"id: %ld\ndata: ",(long)time(NULL));
    snprintf(outbuffer,SSE_PAYLOADMAX,"%s%s\n\n\r\n",
                vcomms_chunk_encoding_idbuffer,
                message);
    return 0;
}

int vcomms_chunk_encoding(char *outbuffer,char *message)
{
    chop(message);
    snprintf(vcomms_chunk_encoding_idbuffer,60,"id: %ld\ndata: ",(long)time(NULL));
    snprintf(outbuffer,SSE_PAYLOADMAX,"%x\r\n%s%s\n\n\r\n",(unsigned int)
                (strlen(vcomms_chunk_encoding_idbuffer) + strlen(message)+2), //+2 is the two \n
                vcomms_chunk_encoding_idbuffer,
                message);
    return 0;
}

char *chop(char *str)
{
    char *c = str;
    c += strlen(str) - 1;
    while(c != str && (*c == '\n' || *c == '\r'))
    {
        *c = '\0';
        --c;
    }
    return str;
}

int get_pos(char *str,char c)
{
    int i;
    for(i=0;i<strlen(str);++i)
        if(str[i] == c)
            return i;
    return -1;
}

int emptyStr(char *str)
{
    if(str[0] == '\0')
        return 1;
    return 0;
}

char* nextNotWhitespace(char *str)
{
    while(!isspace(*str) && *str != '\0')
        ++str;
    while(isspace(*str) && *str != '\0')
        ++str;
    return str;
}

int startWithStr(char *str,const char *pattern)
{
    while(1)
    {
        if(*pattern == '\0')
            return 1;
        if(*str == '\0')
            return 0;
        if(*str != *pattern)
            return 0;
        ++str;
        ++pattern;
    }
}

// -------------------------- http resposes ------------------------------ //

int vcomms_send_handshake(struct CliConn *client)
{
    char buffer[1024];
    char tbuff[64];

    create_time_line(tbuff);
    snprintf(buffer,1024,
            "HTTP/1.1 200 OK\r\n"
            "%s\r\n"
            "Server: VisionOn (%s)\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET\r\n"
            "Access-Control-Allow-Headers: cache-control, last-event-id, X-Requested-With\r\n"
            "Cache-Control: no-cache\r\n"
            "X-Accel-Buffering: no\r\n"
            ///"Keep-Alive: timeout=300, max=100\r\n"
            "Connection: Keep-Alive\r\n"
            ///"Transfer-Encoding: chunked\r\n"
            "Content-Type: text/event-stream\r\n\r\n",
                tbuff,
                pvisset->service);

    cio_high_write(client,buffer);
    client->status = STATUS_COMM;
    return 0;
}

int vcomms_send_badreq(struct CliConn *client)
{
    char buffer[1024];
    char tbuff[64];

    client->status = STATUS_END;
    create_time_line(tbuff);
    snprintf(buffer,1024,
            "HTTP/1.1 400 Bad Request\r\n"
            "%s\r\n"
            "Server: VisionOn (%s)\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "Content-Type: text/html; charset=utf8\r\n\r\n",
            tbuff,
            pvisset->service,
            (int)strlen(badrequest_html));
    cio_high_write(client,buffer);
    cio_high_write(client,badrequest_html);
    return 0;
}

int vcomms_send_notfound(struct CliConn *client)
{
    char buffer[1024];
    char tbuff[64];

    client->status = STATUS_END;
    create_time_line(tbuff);
    snprintf(buffer,1024,
            "HTTP/1.1 404 Not Found\r\n"
            "%s\r\n"
            "Server: VisionOn (%s)\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "Content-Type: text/html; charset=utf8\r\n\r\n",
            tbuff,
            pvisset->service,
            (int)strlen(notfound_html));
    cio_high_write(client,buffer);
    cio_high_write(client,notfound_html);
    return 0;
}

int vcomms_send_notsupported(struct CliConn *client)
{
    char buffer[1024];
    char tbuff[64];

    client->status = STATUS_END;
    create_time_line(tbuff);
    snprintf(buffer,1024,
            "HTTP/1.1 501 Not Implemented\r\n"
            "%s\r\n"
            "Server: VisionOn (%s)\r\n"
            "Allow: GET, HEAD\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET\r\n"
            "Access-Control-Allow-Headers: cache-control, last-event-id, X-Requested-With\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "Content-Type: text/html; charset=utf8\r\n\r\n",
                tbuff,
                pvisset->service,
                (int)strlen(badrequest_html));
    cio_high_write(client,buffer);
    cio_high_write(client,badrequest_html);
    return 0;
}

int vcomms_send_head(struct CliConn *client)
{
    char buffer[1024];
    char tbuff[64];

    client->status = STATUS_END;
    create_time_line(tbuff);
    snprintf(buffer,1024,
            "HTTP/1.1 200 OK\r\n"
            "%s\r\n"
            "Accept: text/*\r\n"
            "Server: VisionOn (%s)\r\n"
            "Cache-control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET\r\n"
            "Access-Control-Allow-Headers: cache-control, last-event-id, X-Requested-With\r\n"
            "Allow: GET, HEAD, OPTIONS\r\n"
            //////"Keep-Alive: timeout=300, max=100\r\n"
            "Content-Type: text/event-stream\r\n\r\n",
                tbuff,
                pvisset->service);
    cio_high_write(client,buffer);
    return 0;
}

int vcomms_send_options(struct CliConn *client)
{
    char buffer[1024];
    char tbuff[64];

    client->status = STATUS_END;
    create_time_line(tbuff);
    snprintf(buffer,1024,
            "HTTP/1.1 200 OK\r\n"
            "%s\r\n"
            "Accept: text/*\r\n"
            "Server: VisionOn (%s)\r\n"
            "Cache-control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET\r\n"
            "Access-Control-Allow-Headers: cache-control, last-event-id, X-Requested-With\r\n"
            "Allow: GET, HEAD, OPTIONS\r\n"
            "Accept-Encoding:\r\n"
            ///"Keep-Alive: timeout=300, max=100\r\n"
            "\r\n",
                tbuff,
                pvisset->service);

    cio_high_write(client,buffer);
    return 0;
}

//end.
