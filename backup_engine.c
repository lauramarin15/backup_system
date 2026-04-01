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


/*
 * sys_smart_copy() — motor de copia con syscalls directas.
 * Por ahora solo retorna SC_OK sin hacer nada.
 * Iremos llenando el cuerpo paso a paso.
 */
int
sys_smart_copy(const char *src_path,
               const char *dst_path,
               sc_result_t *result)
{

    return 0; /* temporal — solo para que compile */
}
