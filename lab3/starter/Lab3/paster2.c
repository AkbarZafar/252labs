#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <time.h>
#include <sys/time.h>
#include "./lab_png.h"
#include "./crc.c"
#include "./zutil.c"

#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 10000  /* 10,000 bytes */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct recv_buf2 {
    char buf[BUF_SIZE];       /* memory to hold a copy of received data */
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

typedef struct circleQ {
    int front;
    int rear;
    int SIZE;
    RECV_BUF* items; 
} CIRCLE_Q;

CIRCLE_Q* queue;

/* circle queue replicated from programiz.com */

void addQueue( RECV_BUF* element ) {
    if( queue->front == -1 ) {
        queue->front = 0;
    }

    queue->rear = (queue->rear + 1) % queue->SIZE;
    memcpy( &( queue->items[queue->rear] ), element, sizeof( RECV_BUF ) );
    return;
}

void removeQueue( RECV_BUF* element ){
    memcpy( element, &(queue->items[queue->front]), sizeof(RECV_BUF));

    if( queue->front == queue->rear ) {
        queue->front = -1;
        queue->rear = -1;
    } else {
        queue->front = ( queue->front + 1) % queue->SIZE;
    }
    return;
}

int init_shm_queue( CIRCLE_Q* p, int stack_size ) {
    p->SIZE = stack_size;
    p->front = -1;
    p->rear = -1;
    p->items = (RECV_BUF*) ( (void*) p + sizeof( CIRCLE_Q ) ); 
    
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
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata) {
    int realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF*) userdata;
    
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
    
    memset(ptr->buf, 0, BUF_SIZE);
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

    U8 decomp_data[BUF_SIZE];
    unsigned long decomp_length;
} Image;

Image* img_arr;
unsigned long* total_data_length;
sem_t* totalDataSem;

int catpng() {
    FILE* f = fopen("all.png", "w");
    FILE* f2 = fopen( "./output_0.png", "rb"); 


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

    U32 width_htonl = htonl(400);
    U32 height_htonl = htonl(300);
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
    U8* total_data = (U8*)malloc(*total_data_length); 
    for( int i=0; i < 50; i++ ){
        memcpy( &total_data[cursor], img_arr[i].decomp_data, img_arr[i].decomp_length );
        cursor += img_arr[i].decomp_length;
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
    
    fclose( f );
    fclose( f2 );
    return 0;

}

int* producerImageCounter;
int* consumerImageCounter;
int imageNumber = 1;
int servernum = 0;
int SLEEP_TIME = 0;

sem_t* prodimgCounterSem;
sem_t* consimgCounterSem;
sem_t* queueSem;
sem_t* prodCounterSem;
sem_t* consCounterSem;

void getImage(){
    CURL *curl_handle;
    RECV_BUF recv_buf;
    CURLcode res;
    char url[256];
    int run = 1;
    int part = 0;

    sem_wait( prodimgCounterSem );
    run = 0;
    (*producerImageCounter)++;
    if( *producerImageCounter < 50 ) {
        run = 1;
        part = *producerImageCounter;
    }
    sem_post( prodimgCounterSem );
    while( run ) {
        sprintf( url, "http://ece252-1.uwaterloo.ca:2530/image?img=%i&part=%i", imageNumber, part);
        /* init a curl session */
        curl_handle = curl_easy_init();

        if (curl_handle == NULL) {
            fprintf(stderr, "curl_easy_init: returned NULL\n");
            return;
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

        /* get it! */
        recv_buf_init(&recv_buf, BUF_SIZE);
        res = curl_easy_perform(curl_handle);

        if( res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } 
        sem_wait( prodCounterSem );        
        sem_wait( queueSem ); 
        addQueue( &recv_buf ); 
        sem_post( queueSem );
        sem_post( consCounterSem );    

        sem_wait( prodimgCounterSem );
        run = 0;
        (*producerImageCounter)++;
        part = *producerImageCounter; 
        if( *producerImageCounter < 50 ) {
            run = 1;

        }
        sem_post( prodimgCounterSem );
        
    }
    /* cleaning up */
    curl_easy_cleanup(curl_handle);
    return ;
}

void producerWork() {
    getImage();
}

void extractIDAT( RECV_BUF* buf ) {
    char fname[256];
    sprintf(fname, "./output_%d.png", buf->seq); 
    write_file( fname, buf->buf, buf->size );

    unsigned long chunk_length;
    U8* compress_data;        

    FILE* f = fopen(fname, "rb");
        
    if ( f == NULL ) {
        perror("file does not exist\n");
        return;
    }
    
    U8 *header = (U8*)malloc(8);
        
    fread(header, 1, 8, f);

    if( !((U8)header[0] == 0x89 && header[1] == 'P' 
            && header[2] == 'N' && header[3] == 'G'
            && (U8)header[4] == 0x0D && (U8)header[5] == 0x0A
            && (U8)header[6] == 0x1A && (U8)header[7] == 0x0A) ) {
        printf("%s: Not a PNG file\n", fname);
        return;
    }

    free( header );
    
    fseek(f, 8, SEEK_CUR);
    fread(&(img_arr[buf->seq].width), 1, 4, f);
    fread(&(img_arr[buf->seq].height), 1, 4, f);
    
    fseek(f, 9, SEEK_CUR);

    fread(&(chunk_length), 1, 4, f);


    compress_data = (U8*) malloc(htonl(chunk_length));
    fseek(f, 4, SEEK_CUR);
    fread(compress_data, 1, htonl(chunk_length), f);
    fseek(f, 4, SEEK_CUR);

        

    mem_inf( img_arr[buf->seq].decomp_data, &img_arr[buf->seq].decomp_length, compress_data, htonl(chunk_length)); 

    sem_wait( totalDataSem );
    *(total_data_length) += img_arr[buf->seq].decomp_length;
    sem_post( totalDataSem );    

        
    free( compress_data );

    fclose(f);
}

void consumerWork() {
    usleep( SLEEP_TIME * 1000 );
    int run = 0;
    sem_wait( consimgCounterSem );
    run = 0;
    (*consumerImageCounter)++;
        
    if( *consumerImageCounter < 50 ) {
        run = 1;

    }
    sem_post( consimgCounterSem );

    while( run ) {
        RECV_BUF* buf = (RECV_BUF*) malloc(sizeof(RECV_BUF));

        sem_wait( consCounterSem );
        sem_wait( queueSem );
        
        removeQueue( buf );
        extractIDAT( buf );


        sem_post( queueSem );
        sem_post( prodCounterSem );

        sem_wait( consimgCounterSem );
        run = 0;
        (*consumerImageCounter)++;
        
        if( *consumerImageCounter < 50 ) {
            run = 1;

        }
        sem_post( consimgCounterSem );
    }
}

int main( int argc, char** argv ) {
    struct timeval tv;
    double times[2];

    if( gettimeofday(&tv, NULL) != 0 ) {
        perror("gettimeofday");
        abort();
    }

    times[0] = (tv.tv_sec) + tv.tv_usec/1000000;
    
    int B = atoi(argv[1]);
    int P = atoi(argv[2]);
    int C = atoi(argv[3]);
    int X = atoi(argv[4]);
    int N = atoi(argv[5]);

    imageNumber = N;
    SLEEP_TIME = X;
    int SHM_SIZE = (sizeof(CIRCLE_Q)) + (sizeof(RECV_BUF) * B);

    int shmQueueId = shmget( IPC_PRIVATE, SHM_SIZE, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    queue = (CIRCLE_Q*) shmat( shmQueueId, NULL, 0);
    init_shm_queue( queue, B ); 

    int shmprodImageCounterId = shmget( IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    producerImageCounter = (int*) shmat( shmprodImageCounterId, NULL, 0);
    *producerImageCounter = -1; 
    
    int shmconsImageCounterId = shmget( IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    consumerImageCounter = (int*) shmat( shmconsImageCounterId, NULL, 0);
    *consumerImageCounter = -1; 

    int shmQueueSemId =  shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    queueSem = (sem_t*) shmat( shmQueueSemId, NULL, 0);
    sem_init( queueSem, 1, 1 );

    int shmprodIMGCounterSemId = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    prodimgCounterSem = (sem_t*) shmat( shmprodIMGCounterSemId, NULL, 0);
    sem_init( prodimgCounterSem, 1, 1);    

    int shmconsIMGCounterSemId = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); 
    consimgCounterSem = (sem_t*) shmat( shmconsIMGCounterSemId, NULL, 0);
    sem_init( consimgCounterSem, 1, 1);    
    
    int shmProdQueueSpaceId = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    prodCounterSem = (sem_t*) shmat( shmProdQueueSpaceId, NULL, 0);
    sem_init( prodCounterSem, 1, B );
    
    int shmConsQueueSpaceId = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    consCounterSem = (sem_t*) shmat( shmConsQueueSpaceId, NULL, 0);
    sem_init( consCounterSem, 1, 0 );

    int shmImgArr = shmget(IPC_PRIVATE, sizeof(Image)*50, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    img_arr = (Image*) shmat( shmImgArr, NULL, 0 );

    int shmtotDataId = shmget(IPC_PRIVATE, sizeof(Image)*50, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    total_data_length = (unsigned long*) shmat( shmtotDataId, NULL, 0 );
    *(total_data_length) = 0;

    int shmtotDataSemId = shmget(IPC_PRIVATE, sizeof(Image)*50, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    totalDataSem = (sem_t*) shmat( shmtotDataSemId, NULL, 0 );
    sem_init(totalDataSem, 1, 1);


    pid_t prod_pids[P];
    pid_t cons_pids[C];

    pid_t pid;
    int state;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    for( int i = 0; i < P; i++) {
        pid = fork();
        if( pid == 0 ) {
            producerWork();
            exit(0);
        } else {
            prod_pids[i] = pid;
        }
    }

    for( int i = 0; i < C; i++ ) {
        pid = fork();
        if( pid == 0 ) {
            consumerWork();
            exit(0);
        } else {
            cons_pids[i] = pid;
        }
    }

    if ( pid > 0 ) {
        for( int i = 0; i < P; i++ ) {
            waitpid(prod_pids[i], &state, 0);
        }

        for( int i = 0; i< C; i++ ) {
            waitpid(cons_pids[i], &state, 0);
        }
    }
    
    /* cleaning up */

    
    catpng( );
    
    if( gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("paster2 execution time: %.6lf seconds\n", times[1] - times[0] );

    shmctl( shmQueueId, IPC_RMID, NULL );
    shmdt( queue );

    shmctl( shmprodImageCounterId, IPC_RMID, NULL );
    shmdt( producerImageCounter );

    shmctl( shmconsImageCounterId, IPC_RMID, NULL );
    shmdt( consumerImageCounter );

    shmctl( shmQueueSemId, IPC_RMID, NULL );
    shmdt( queueSem );

    shmctl( shmprodIMGCounterSemId, IPC_RMID, NULL );
    shmdt( prodimgCounterSem );

    shmctl( shmconsIMGCounterSemId, IPC_RMID, NULL );
    shmdt( consimgCounterSem );
 
    shmctl( shmProdQueueSpaceId, IPC_RMID, NULL );
    shmdt( prodCounterSem );
  
    shmctl( shmConsQueueSpaceId, IPC_RMID, NULL );
    shmdt( consCounterSem );
  
    shmctl( shmImgArr, IPC_RMID, NULL );
    shmdt( img_arr );
  
    shmctl( shmtotDataId, IPC_RMID, NULL );
    shmdt( total_data_length );
  
    shmctl( shmtotDataSemId, IPC_RMID, NULL );
    shmdt( totalDataSem );
  

    curl_global_cleanup();
    return 0;
}


