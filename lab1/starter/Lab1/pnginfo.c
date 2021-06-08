#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../png_util/lab_png.h"
#include "../png_util/crc.c"
#include <arpa/inet.h>

#define BUF_LEN2 (256*32)

int main( int argc, char **argv ) {
    
    if( argc < 2 ) {
        printf("Please specify a file\n");
        return -1;
    } 

    struct stat buf;

    if (lstat(argv[1], &buf) < 0) {
        printf("lstat error\n");
        return -1;
    } 
    if (!S_ISREG(buf.st_mode)) {
        printf("%s: Not a PNG file\n", argv[1]);
        return -1;
    }  

    FILE* f = fopen(argv[1], "rb");

    if ( f == NULL ) {
        perror("file does not exist\n");
    }
    
    U8 *header = (U8*)malloc(8);

    fread(header, 1, 8, f);

    if( !((U8)header[0] == 0x89 && header[1] == 'P' 
           && header[2] == 'N' && header[3] == 'G'
           && (U8)header[4] == 0x0D && (U8)header[5] == 0x0A
           && (U8)header[6] == 0x1A && (U8)header[7] == 0x0A) ) {
        printf("%s: Not a PNG file\n", argv[1]);
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
    
    printf("%s: %i x %i\n", argv[1], htonl(width), htonl(height));

    U32 crc_val = crc(chunk1->type, 4) ^ 0xffffffffL;
    crc_val = update_crc(crc_val, chunk1->p_data, htonl(chunk1->length)) ^ 0xffffffffL; 
    if( htonl(chunk1->crc) != crc_val ) {
        printf("IHDR chunk CRC error: computed %x, expected %x\n", crc_val, htonl(chunk1->crc));
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
        printf("IDAT chunk CRC error: computed %x, expected %x\n", crc_val, htonl(chunk2->crc));
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
        printf("IEND chunk CRC error: computed %x, expected %x\n", crc_val, htonl(chunk3->crc));
        return -1;
    }
    

    free( chunk3->p_data );
    free( chunk3 ); 
    
    fclose( f );

    
    return 0;
}
