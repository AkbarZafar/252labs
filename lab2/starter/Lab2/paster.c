#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include "./lab_png.h"
#include "./crc.c"
#include <arpa/inet.h>
#include "./zutil.c"

#define IMG_URL "http://ece252-1.uwaterloo.ca:2520/image?img=1"
#define DUM_URL "https://example.com/"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

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


size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
int write_file(const char *path, const void *in, size_t len);


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
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata) {
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;
    
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

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata) {
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


int recv_buf_init(RECV_BUF *ptr, size_t max_size) {
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
    ptr->seq = -1;              /* valid seq should be non-negative */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr) {
    if (ptr == NULL) {
	return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}


/**
 * @brief output data in memory to a file
 * @param path const char *, output file path
 * @param in  void *, input data to be written to the file
 * @param len size_t, length of the input data in bytes
 */

int write_file(const char *path, const void *in, size_t len) {
    FILE *fp = NULL;

    if (path == NULL) {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL) {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        perror("fopen");
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len) {
        fprintf(stderr, "write_file: imcomplete write!\n");
        return -3; 
    }
    return fclose(fp);
}

typedef struct image {
    U32 height;
    U32 width;

    U8* decomp_data;
    unsigned long decomp_length;
} Image;

int catpng( char* imageName[50] ) {

    U32 height_final = 0;
    U32 width_final = 0;
        
    Image* img_arr = (Image*)malloc(sizeof(Image)*(50));
    unsigned long total_data_length = 0;


    for( int i=0; i < 50 ; i++) {
        unsigned long chunk_length;
        U8* compress_data;        
        unsigned long decomp_length;

        FILE* f = fopen(imageName[i], "rb");
        
        if ( f == NULL ) {
            perror("file does not exist\n");
            return -1;
        }
    
        U8 *header = (U8*)malloc(8);
        
        fread(header, 1, 8, f);

        if( !((U8)header[0] == 0x89 && header[1] == 'P' 
                && header[2] == 'N' && header[3] == 'G'
                && (U8)header[4] == 0x0D && (U8)header[5] == 0x0A
                && (U8)header[6] == 0x1A && (U8)header[7] == 0x0A) ) {
            printf("%s: Not a PNG file\n", imageName[i]);
            return -1;
        }

        free( header );
    
        fseek(f, 8, SEEK_CUR);
        fread(&(img_arr[i].width), 1, 4, f);
        fread(&(img_arr[i].height), 1, 4, f);

        if(width_final == 0) {
            width_final = htonl(img_arr[i].width);
        }
        height_final += htonl(img_arr[i].height);
    
        fseek(f, 9, SEEK_CUR);

        fread(&(chunk_length), 1, 4, f);


        compress_data = (U8*) malloc(htonl(chunk_length));
        fseek(f, 4, SEEK_CUR);
        fread(compress_data, 1, htonl(chunk_length), f);
        fseek(f, 4, SEEK_CUR);

        
        decomp_length = img_arr[i].height * (img_arr[i].width * 4 + 1 );

        img_arr[i].decomp_data = (U8*) malloc(decomp_length);

        mem_inf( img_arr[i].decomp_data, &img_arr[i].decomp_length, compress_data, htonl(chunk_length)); 
        total_data_length += img_arr[i].decomp_length;
        
        free( compress_data );

        fclose(f);
        

    }

    
    FILE* f = fopen("all.png", "w");
    FILE* f2 = fopen( imageName[0], "rb"); 


    U8* header_prev = (U8*)malloc(8);
    fread(header_prev, 1, 8, f2);


    fwrite( header_prev, sizeof(U8), 8, f);
    
    free( header_prev );
    
    U8* IHDR_len = (U8*)malloc(4);
    U8* IHDR_type = (U8*)malloc(4);
    fread( IHDR_len, 1, 4, f2);
    fread( IHDR_type, 1, 4, f2);
    fwrite( IHDR_len, sizeof(U8), 4, f);
    fwrite( IHDR_type, sizeof(U8), 4, f);

    free( IHDR_len );

    U8* data = (U8*) malloc(13);
    fread( data, 1, 13, f2);

    U32 width_htonl = htonl(width_final);
    U32 height_htonl = htonl(height_final);
    fwrite( &width_htonl, sizeof(U8), 4, f);
    fwrite( &height_htonl, sizeof(U8), 4, f);

    fwrite( &data[8], sizeof(U8), 5, f );
    
    U8 a[4];
    U32 crc_val = crc(IHDR_type, 4) ^ 0xffffffffL;
    memcpy(a, &width_htonl, 4);
    crc_val = update_crc(crc_val, a, 4);

    memcpy(a, &height_htonl, 4);
    crc_val = update_crc(crc_val, a, 4);

    crc_val = update_crc(crc_val, data+8, 5) ^ 0xffffffffL; 
    U32 crc_htonl = htonl(crc_val);
    
    free( data );
    free( IHDR_type );

    fwrite( &crc_htonl, sizeof(U8), 4, f);
    
    fseek(f2, 8, SEEK_CUR);
    unsigned long cursor = 0;
    U8* total_data = (U8*)malloc(total_data_length); 

    for( int i=0; i < 50; i++ ){
        memcpy( &total_data[cursor], img_arr[i].decomp_data, img_arr[i].decomp_length );
        cursor += img_arr[i].decomp_length;

        free( img_arr[i].decomp_data ); 
    }
    U8* comp_data = (U8*)malloc(cursor + 100);
    unsigned long comp_data_length = 0;
    mem_def( comp_data, &comp_data_length, total_data, cursor, Z_DEFAULT_COMPRESSION);
    
    U8* type = (U8*)malloc(4);
    fread(type, 1, 4, f2);
    
    unsigned long comp_length_htonl = htonl(comp_data_length);

    fwrite( &comp_length_htonl, sizeof(U8), 4, f); 
    fwrite( type, sizeof(U8), 4, f);

    fwrite( comp_data, sizeof(U8), comp_data_length, f); 

    crc_val = crc(type, 4) ^ 0xffffffffL;

    crc_val = update_crc(crc_val, comp_data, comp_data_length) ^ 0xffffffffL; 
    
    crc_htonl = htonl(crc_val);

    fwrite( &crc_htonl, sizeof(U8), 4, f);

    free( comp_data );
    free( total_data );
    free( type );
    
    fseek(f2, 8+img_arr[0].decomp_length, SEEK_CUR);
    U8 IEND[12] = {0,0,0,0,73,69,78,68,174,66,96,130};
    
    fwrite( IEND, 1, 12, f);
    
    free(img_arr);

    fclose( f );
    fclose( f2 );
    return 0;

}

int main( int argc, char** argv ) {
    CURLcode res;
    char fname[256];
    CURL *curl_handle;
    char url[256];
    RECV_BUF recv_buf;
    pid_t pid =getpid();
    
    
    if (argc == 1) {
        strcpy(url, IMG_URL); 
    } else {
        strcpy(url, argv[1]);
    }
    printf("%s: URL is %s\n", argv[0], url);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return 1;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    int i = 0;
    int flags[50];
    memset( flags, 0, sizeof(int)*50 );
    char* imageNames[50];
    /* get it! */
    while( i < 50 ) {
        recv_buf_init(&recv_buf, BUF_SIZE);
        res = curl_easy_perform(curl_handle);

        if( res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
	    printf("%lu bytes received in memory %p, seq=%d. i: %i \n", \
            recv_buf.size, recv_buf.buf, recv_buf.seq, i);
        }

        if( flags[recv_buf.seq] == 0 ) {
            flags[recv_buf.seq] = 1;
            sprintf(fname, "./output_%d_%d.png", recv_buf.seq, pid);
            imageNames[recv_buf.seq] = (char*) malloc(sizeof(fname));
            memcpy( imageNames[recv_buf.seq], fname, sizeof(fname) );
            write_file(fname, recv_buf.buf, recv_buf.size);
            i++;
        }
        recv_buf_cleanup(&recv_buf);
        
    }
    
    printf("got all the files \n"); 
    catpng( imageNames );
    /* cleaning up */
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    return 0;
}




