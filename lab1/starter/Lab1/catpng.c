#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "./lab_png.h"
#include "./crc.c"
#include <arpa/inet.h>
#include "./zutil.c"

typedef struct image {
    U32 height;
    U32 width;

    U8* decomp_data;
    unsigned long decomp_length;
} Image;

int main(int argc, char** argv) {
    
    if( argc < 2 ) {
        printf("invalid\n");
        return -1;
    }

    U32 height_final = 0;
    U32 width_final = 0;
        
    Image* img_arr = (Image*)malloc(sizeof(Image)*(argc-1));
    unsigned long total_data_length = 0;


    for( int i=0; i< argc-1 ; i++) {
        
        unsigned long chunk_length;
        U8* compress_data;        
        unsigned long decomp_length;

        FILE* f = fopen(argv[i+1], "rb");
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
            printf("%s: Not a PNG file\n", argv[i+1]);
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
    FILE* f2 = fopen( argv[1], "rb"); 


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

    for( int i=0; i < argc-1; i++ ){
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

