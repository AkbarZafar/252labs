#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include <search.h>
#include <openssl/sha.h>
#include <semaphore.h>
#include <pthread.h>
#include <search.h>
#include "./lab_png.h"
#include "./crc.c"
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include <curl/multi.h>

#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4/"
#define OUTPUT_FILE "log.txt"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */
#define MAX_WAIT_MSECS 30*1000 /* Wait max. 30 seconds */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct list {
    size_t length;
    char* value[10000];
} LIST;

LIST toVisit;
int m = 50;
char* v;
int PNGCount = 0;
int connections = 1;

char* entries[10000];
int entriesLen;

htmlDocPtr mem_getdoc(char *buf, int size, const char *url);
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath);
int find_http(char *fname, int size, int follow_relative_links, const char *base_url);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
void cleanup(CURL *curl, RECV_BUF *ptr);
int write_file(const char *path, const void *in, size_t len);
CURL *easy_handle_init(RECV_BUF *ptr, const char *url);
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf);
void addToVisitList( xmlChar* url );
void popVisitList(char* dest);
int checkIfPNG( char* fileName );

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        return NULL;
    }
    return doc;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath){
	
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        return NULL;
    }
    return result;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{

    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
		
    if (buf == NULL) {
        return 1;
    }
    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if ( href != NULL && !strncmp((const char *)href, "http", 4) ) {
                addToVisitList( href );
            } else {
                xmlFree( href );
            }
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    return 0;
}
/**
 * @brief  cURL header call back function to extract image sequence number from 
 *         http header data. An example header for image part n (assume n = 2) is:
 *         X-Ece252-Fragment: 2
 * @param  char *p_recv: header data delivered by cURL
 * @param  size_t size size of each memb
 * @param  size_t nmemb number of memb
 * @param  void *userdata user defined data structurea
 * @return size of header data received.
 * @details this routine will be invoked multiple times by the libcurl until the full
 * header data are received.  we are only interested in the ECE252_HEADER line 
 * received so that we can extract the image sequence number from it. This
 * explains the if block in the code.
 */
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

// #ifdef DEBUG1_
   // printf("%s", p_recv);
// #endif /* DEBUG1_ */
    if (realsize > strlen(ECE252_HEADER) &&
	strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

        /* extract img sequence number */
	p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realsize;
}


/**
 * @brief write callback function to save a copy of received data in RAM.
 *        The received libcurl data are pointed by p_recv, 
 *        which is provided by libcurl and is not user allocated memory.
 *        The user allocated memory is at p_userdata. One needs to
 *        cast it to the proper struct to make good use of it.
 *        This function maybe invoked more than once by one invokation of
 *        curl_easy_perform().
 */

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}


int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be positive */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    free( ptr );
    return 0;
}

void cleanup(CURL *curl, RECV_BUF *ptr)
{
        curl_easy_cleanup(curl);
        recv_buf_cleanup(ptr);
}
/**
 * @brief output data in memory to a file
 * @param path const char *, output file path
 * @param in  void *, input data to be written to the file
 * @param len size_t, length of the input data in bytes
 */

int write_file(const char *path, const void *in, size_t len)
{
    FILE *fp = NULL;

    if (path == NULL) {
        return -1;
    }

    if (in == NULL) {
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len) {
        return -3; 
    }
    return fclose(fp);
}

/**
 * @brief create a curl easy handle and set the options.
 * @param RECV_BUF *ptr points to user data needed by the curl write call back function
 * @param const char *url is the target url to fetch resoruce
 * @return a valid CURL * handle upon sucess; NULL otherwise
 * Note: the caller is responsbile for cleaning the returned curl handle
 */

CURL *easy_handle_init(RECV_BUF *ptr, const char *url)
{
    CURL *curl_handle = NULL;

    if ( ptr == NULL || url == NULL) {
        return NULL;
    }

    /* init user defined call back function buffer */
    if ( recv_buf_init(ptr, BUF_SIZE) != 0 ) {
        return NULL;
    }
    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab4 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    /* Max time in seconds that the connection phase to the server to take */
    //curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
    /* Max time in seconds that libcurl transfer operation is allowed to take */
    //curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    /* Time out for Expect: 100-continue response in milliseconds */
    //curl_easy_setopt(curl_handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    curl_easy_setopt( curl_handle, CURLOPT_PRIVATE, (void *)ptr);

    return curl_handle;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char fname[256];
    int follow_relative_link = 1;
    char *url = NULL; 

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url); 
    sprintf(fname, "./output.html");
    return write_file(fname, p_recv_buf->buf, p_recv_buf->size);
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char fname[256];
    char *eurl = NULL;          /* effective URL */
    char urlToWrite[1000];

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    
    sprintf(fname, "./output_%d.png", p_recv_buf->seq);
    write_file(fname, p_recv_buf->buf, p_recv_buf->size);
    
    if( checkIfPNG( fname ) != 0 ) { 
        return -1;
    } 
    

    if ( eurl != NULL) {
        if ( PNGCount < m ) {
            FILE* f = fopen( "png_urls.txt", "a" );
        
            sprintf( urlToWrite, "%s\n", eurl);

            fwrite( urlToWrite, 1, strlen(urlToWrite), f );
            PNGCount++;
            fclose( f );
        }
    }
    return 0;
}
/**
 * @brief process teh download data by curl
 * @param CURL *curl_handle is the curl handler
 * @param RECV_BUF p_recv_buf contains the received data. 
 * @return 0 on success; non-zero otherwise
 */

int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    CURLcode res;
    char fname[256];
    long response_code;

    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);

    if ( response_code >= 400 ) { 
        return 1;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if ( res == CURLE_OK && ct != NULL ) {
    } else {
        return 2;
    }

    if ( strstr(ct, CT_HTML) ) {
        return process_html(curl_handle, p_recv_buf);
    } else if ( strstr(ct, CT_PNG) ) {
        return process_png(curl_handle, p_recv_buf);
    } else {
        sprintf(fname, "./output_%s", ct);
    }

    
    return write_file(fname, p_recv_buf->buf, p_recv_buf->size);
}

void addToVisitList( xmlChar* url ) {
    ENTRY data;
    data.key = (char*) url;
    data.data = (void*) 1;
    
    char urlToWrite[1000];
    
    

    if (hsearch( data, FIND ) == NULL ) {
        hsearch( data, ENTER );
        //toVisit.length +=1;

        entries[entriesLen] = (char*) url;   
        entriesLen++;

        toVisit.value[ toVisit.length ] = data.key;
        toVisit.length++;
        if( v != NULL ) {
            FILE* f = fopen( v, "a" );
            sprintf( urlToWrite, "%s\n", data.key);
            fwrite( urlToWrite, 1, strlen(urlToWrite), f );
            fclose( f );
        }
    } else {
        xmlFree( url );
    }
}

void popVisitList(char* dest) {

    if( toVisit.length > 0 ) {
        strcpy(dest, toVisit.value[ toVisit.length - 1 ]);
        toVisit.length -=1;
    }
    
}

int checkIfPNG( char* fileName ) {

    FILE* f = fopen(fileName, "rb");

    if ( f == NULL ) {
        perror("file does not exist\n");
    }
    
    U8 *header = (U8*)malloc(8);

    fread(header, 1, 8, f);

    if( !((U8)header[0] == 0x89 && header[1] == 'P' 
           && header[2] == 'N' && header[3] == 'G'
           && (U8)header[4] == 0x0D && (U8)header[5] == 0x0A
           && (U8)header[6] == 0x1A && (U8)header[7] == 0x0A) ) {
        free( header );
        fclose(f);
        return -1;
    }
    
    free( header );

    chunk_p chunk1 = malloc(sizeof(struct chunk));
    chunk1->p_data = malloc(13);
    fread(&(chunk1->length), 1, 4, f);
    fread(&(chunk1->type), 1, 4, f);
    fread(chunk1->p_data, 1, 13, f);
    fseek(f, -13, SEEK_CUR);
    U32 width;
    U32 height;

    fread(&width, 1, 4, f);
    fread(&height, 1, 4, f);
    fseek(f, 5, SEEK_CUR);
    fread(&(chunk1->crc), 1, 4, f);
    
    U32 crc_val = crc(chunk1->type, 4) ^ 0xffffffffL;
    crc_val = update_crc(crc_val, chunk1->p_data, htonl(chunk1->length)) ^ 0xffffffffL; 
    if( htonl(chunk1->crc) != crc_val ) {
        fclose(f);
        free( chunk1->p_data );
        free( chunk1 ); 
    
        return -1;
    }
    free( chunk1->p_data );
    free( chunk1 ); 


    chunk_p chunk2 = malloc(sizeof(struct chunk));
    fread(&(chunk2->length), 1, 4, f);

    chunk2->p_data = malloc(htonl(chunk2->length));
    fread(&(chunk2->type), 1, 4, f);
    fread(chunk2->p_data, 1, htonl(chunk2->length), f);
    fread(&(chunk2->crc), 1, 4, f);

    crc_val = crc(chunk2->type, 4) ^ 0xffffffffL;
    crc_val = update_crc(crc_val, chunk2->p_data, htonl(chunk2->length)) ^ 0xffffffffL;
    if( htonl(chunk2->crc) != crc_val ) {
        fclose(f);
        free( chunk2->p_data );
        free( chunk2 ); 
    
        return -1;
    }
    
    free( chunk2->p_data );
    free( chunk2 ); 

    chunk_p chunk3 = malloc(sizeof(struct chunk));
    fread(&(chunk3->length), 1, 4, f);

    chunk3->p_data = malloc(htonl(chunk3->length));
    fread(&(chunk3->type), 1, 4, f);
    fread(chunk3->p_data, 1, htonl(chunk3->length), f);
    fread(&(chunk3->crc), 1, 4, f);

    crc_val = crc(chunk3->type, 4) ^ 0xffffffffL;
    crc_val = update_crc(crc_val, chunk3->p_data, htonl(chunk3->length)) ^ 0xffffffffL;
    if( htonl(chunk3->crc) != crc_val ) {
        fclose(f);
        free( chunk3->p_data );
        free( chunk3 ); 
    
        return -1;
    }
    

    free( chunk3->p_data );
    free( chunk3 ); 
    
    fclose( f );

    return 0;
}

void runner( ) {
    CURL *curl_handle;
    char url[256];
    RECV_BUF* recv_buf;
    
    int run = 1;

    int still_running = 0;
    int msgs_left = 0;

    int MCSize = 0;
    
    CURLMsg *msg=NULL;
    CURLcode return_code=0;


    CURLM* cm = NULL;

    cm = curl_multi_init();

    while( run ) {
        if( PNGCount >= m || ( MCSize == 0 && toVisit.length == 0 ) ) {
            run = 0;
        }

        while( toVisit.length > 0 && MCSize < connections ) {
            popVisitList(url);
            recv_buf = malloc( sizeof( RECV_BUF ) );
            memset( recv_buf, 0, sizeof( RECV_BUF ) );
            curl_handle = easy_handle_init(recv_buf, url);
            curl_multi_add_handle(cm, curl_handle);
            MCSize++;
            if ( curl_handle == NULL ) {
                fprintf(stderr, "Curl initialization failed. Exiting...\n");
                curl_global_cleanup();
                abort();
            }
        }        
        
        if( MCSize <= connections && MCSize > 0) {
        
            /* get it! */
            curl_multi_perform(cm, &still_running);
            
            while ((msg = curl_multi_info_read(cm, &msgs_left)) && (msg->msg == CURLMSG_DONE) ) {
                RECV_BUF *buf;
                CURL *eh = msg->easy_handle;
                    
                return_code = msg->data.result;
                if(return_code!=CURLE_OK) {
                    MCSize--;
                    curl_easy_getinfo(eh, CURLINFO_PRIVATE, &buf);
                    curl_multi_remove_handle(cm, eh);
                    cleanup(eh, buf);
                    continue;
                }

                curl_easy_getinfo(eh, CURLINFO_PRIVATE, &buf);
                process_data(eh, buf);

                curl_multi_remove_handle(cm, eh);
                MCSize--;
                /* cleaning up */
                cleanup(eh, buf);
            }
        /* process the download data */
        }    
    }

    curl_multi_cleanup(cm);

    return;
}

int main( int argc, char** argv ) 
{
    int c;
    char* URL;
    struct timeval tv;
    double times[2];

    while ((c = getopt (argc, argv, "t:m:v:")) != -1) {
        switch (c) {
        case 't':
	    connections = strtoul(optarg, NULL, 10);
	    if (connections <= 0) {
                fprintf(stderr, "%s: option requires an argument > 0 -- 't'\n", argv[0]);
                return -1;
            }
            break;
        case 'm':
            m = strtoul(optarg, NULL, 10);
            break;
        case 'v':
            v = optarg;
            break;
        default:
            return -1;         
        }
    }

    if( gettimeofday(&tv, NULL) != 0 ) {
        perror("gettimeofday");
        abort();
    }

    times[0] = (tv.tv_sec) + tv.tv_usec/1000000;


    // create file, overwrites old file to be clean if exists. 

    URL = argv[argc-1];

    hcreate( 10000 );
    
    entriesLen = 0;

    
    toVisit.length = 0;

    toVisit.value[toVisit.length] = URL;
    toVisit.length++;

    FILE* f = fopen( "png_urls.txt", "w" );
    fclose( f );



    if( v != NULL ) {
        FILE* logger = fopen( v, "w" );
        fclose( logger );
    } 


    xmlInitParser();
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    runner( );

    for( int i = 0; i < entriesLen; i++ ){
        free( entries[i] );

    }

    if( gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("findpng2 execution time: %.6lf seconds\n", times[1] - times[0] );
        
    xmlCleanupParser( );
    hdestroy();    
    curl_global_cleanup();
    return 0;
}


