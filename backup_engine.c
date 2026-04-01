#define _POSIX_C_SOURCE 200809L  /* activa funciones POSIX avanzadas */

#include "smart_copy.h"  /* nuestro propio header */

/* Estos headers nos dan las syscalls POSIX */
#include <stdio.h>       /* fprintf — solo para mensajes, NO para I/O  */
#include <string.h>      /* strerror — convierte errno a texto          */
#include <errno.h>       /* errno, ENOENT, EACCES, ENOSPC              */
#include <time.h>        /* clock_gettime, struct timespec              */
#include <syslog.h>      /* openlog, syslog, closelog                  */
#include <fcntl.h>       /* open(), O_RDONLY, O_WRONLY, O_CREAT        */
#include <unistd.h>      /* read(), write(), close(), access()         */
#include <sys/stat.h>    /* fstat(), struct stat                       */
#include <sys/types.h>   /* off_t, ssize_t                             */

/*timespec_diff() — calcula la diferencia entre dos momentos*/
static double
timespec_diff(const struct timespec *inicio,
              const struct timespec *fin)
{
    /*
     * (fin->tv_sec - inicio->tv_sec)      → diferencia en segundos
     * (fin->tv_nsec - inicio->tv_nsec)    → diferencia en nanosegundos
     * × 1e-9                              → convierte ns a segundos
     *
     * Ejemplo:
     *   inicio: tv_sec=1000, tv_nsec=500000000  (1000.5 s)
     *   fin:    tv_sec=1003, tv_nsec=200000000  (1003.2 s)
     *   resultado: (1003-1000) + (200M-500M)×1e-9 = 3 + (-0.3) = 2.7 s
     */
    return (double)(fin->tv_sec  - inicio->tv_sec) +
           (double)(fin->tv_nsec - inicio->tv_nsec) * 1e-9;
}
/* Todavía no hay lógica. Solo las varibles que voy a usar. */
int
sys_smart_copy(const char *src_path,
               const char *dst_path,
               sc_result_t *result)
{
    /* Código de retorno — empieza en "sin error" */
    int rc = SC_OK;
 
    /* Descriptores de archivo
     * Empiezan en -1 para indicar "todavía no abierto".
     * En el cleanup verificaremos if (fd >= 0) antes de cerrar,
     * así evitamos llamar close() con un fd inválido. */
    int src_fd = -1;
    int dst_fd = -1;
 
    /*
     * Buffer de trabajo — PAGE_SIZE bytes (4096)
     * Está en el STACK (memoria local de la función).
     * Cuando sys_smart_copy() termina, este buffer desaparece.
     *
     * ¿Por qué 4096?
     * La MMU del CPU trabaja en páginas de 4096 bytes.
     * Usar este tamaño alinea nuestras lecturas con
     * la memoria física → más eficiente con el hardware.
     */
    char    buffer[PAGE_SIZE];
 
    /* Cuántos bytes leyó el último read() */
    ssize_t bytes_read;
 
    /* Contadores para sc_result_t */
    off_t    bytes_copied = 0;  /* total transferido              */
    uint32_t read_ops     = 0;  /* veces que llamamos read()      */
    uint32_t write_ops    = 0;  /* veces que llamamos write()     */

    struct timespec t_start, t_end;
 
  
 
    return rc; /* temporal */
}
 
