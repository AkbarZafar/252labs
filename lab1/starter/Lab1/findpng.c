#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include "./lab_png.h"


int png_found;

void print_png(char* file) {
    
    FILE* f = fopen(file, "rb");

    if ( f == NULL ) {
        printf("file %s does not exist\n", file);
    }
    
    U8 *header = (U8*)malloc(8);

    fread(header, 1, 8, f);


    if( (U8)header[0] == 0x89 && header[1] == 'P' 
           && header[2] == 'N' && header[3] == 'G'
           && (U8)header[4] == 0x0D && (U8)header[5] == 0x0A
           && (U8)header[6] == 0x1A && (U8)header[7] == 0x0A ) {
        png_found = 1;
        printf("%s\n", file);
        return;
    }

}

void open_dir( char* dir_name ) {
    struct dirent *p_dirent;
    DIR *p_dir;


    if ((p_dir = opendir(dir_name) ) == NULL) {
        printf( "opendir(%s)\n", dir_name);
        exit(2);
    }

    while ((p_dirent = readdir(p_dir)) != NULL) {
        char *str_path = p_dirent->d_name;  /* relative path name! */
        
        if (str_path == NULL) {
            fprintf(stderr,"Null pointer found!");
            exit(3);
        } else if( strcmp(str_path, ".") != 0 && strcmp(str_path, "..") != 0 ) {
            char* dir = (char*)malloc(strlen(dir_name) + strlen(str_path) + 2);
            sprintf(dir, "%s/%s", dir_name, str_path);
            if( p_dirent->d_type == DT_REG ) {
                print_png( dir );
            } else if ( p_dirent->d_type == DT_DIR ) {
                open_dir( dir );
            }
        }
    }
}


int main(int argc, char** argv){
    png_found = 0;
    if( argc < 2 ) {
        printf("invalid\n");
        return -1;
    }

    if( argv[1] == NULL ) {
        printf("specify a file\n");
        return -1;
    }

    open_dir(argv[1]);
    
    if( png_found == 0 ) {
        printf("findpng: No PNG file found\n");
    }


    return 0;
}


