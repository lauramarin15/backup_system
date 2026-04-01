#define _POSIX_C_SOURCE 200809L

#include "smart_copy.h"  /* nuestro header */

/* Librería estándar C — aquí SÍ usamos stdio para I/O (stdio_copy) */
#include <stdio.h>       /* fopen, fread, fwrite, fclose, printf      */
#include <stdlib.h>      /* exit, EXIT_SUCCESS, EXIT_FAILURE           */
#include <string.h>      /* strcmp, memset                             */
#include <errno.h>       /* errno, strerror                            */
#include <time.h>        /* clock_gettime                              */
#include <unistd.h>      /* unlink                                     */
#include <sys/stat.h>    /* stat                                       */

/* Tamaños para el benchmark */
#define SIZE_1KB   (1024L)
#define SIZE_1MB   (1024L * 1024L)
#define SIZE_1GB   (1024L * 1024L * 1024L)

/* Rutas temporales */
#define TMP_SRC    "/tmp/bk_src.bin"
#define TMP_SMART  "/tmp/bk_dst_smart.bin"
#define TMP_STDIO  "/tmp/bk_dst_stdio.bin"


/*
 * main() — punto de entrada.
 * Por ahora solo imprime un mensaje.
 */
int
main(int argc, char *argv[])
{
    printf("Paso 1 completo — estructura lista\n");
    return EXIT_SUCCESS;
}