#ifndef SMART_COPY_H
#define SMART_COPY_H

/* Estos dos headers nos dan los tipos que vamos a necesitar */
#include <stdint.h>      /* uint32_t — entero de exactamente 32 bits  */
#include <sys/types.h>   /* off_t    — tipo para tamaños de archivo    */

#define PAGE_SIZE   4096          /* 1 página = 4 KiB               */
#define IO_BLOCK    (PAGE_SIZE * 16)  /* buffer real = 64 KiB       */
 
/* ─────────────────────────────────────────────────────────
 * CÓDIGOS DE ERROR
 * 9 tipos de fallo posibles
 * Cada tipo de fallo tiene su propio número negativo.
 * Igual que las syscalls del kernel: 0 = ok, negativo = error.
 */

#define SC_OK            0   /* sin error                           */
#define SC_ERR_OPEN_SRC -1   /* no se pudo abrir el origen          */
#define SC_ERR_OPEN_DST -2   /* no se pudo crear el destino         */
#define SC_ERR_READ     -3   /* error durante la lectura            */
#define SC_ERR_WRITE    -4   /* error durante la escritura          */
#define SC_ERR_PERM     -5   /* permiso denegado (EACCES)           */
#define SC_ERR_NOENT    -6   /* archivo no existe (ENOENT)          */
#define SC_ERR_NOSPC    -7   /* disco lleno (ENOSPC)                */
#define SC_ERR_STAT     -8   /* no se pudo leer metadatos           */


typedef struct {
    int      status;          /* SC_OK o código SC_ERR_*            */
    off_t    bytes_copied;    /* total de bytes transferidos         */
    uint32_t read_ops;        /* cuántas veces se llamó read()      */
    uint32_t write_ops;       /* cuántas veces se llamó write()     */
    double   elapsed_sec;     /* tiempo total en segundos            */
    double   throughput_mbps; /* velocidad: bytes/tiempo/1MB         */
} sc_result_t;
 
/* ─── Prototipo — implementado en backup_engine.c ─────── */
int         sys_smart_copy(const char *src_path,
                           const char *dst_path,
                           sc_result_t *result);
const char *sc_strerror(int err_code);
void        sc_print_result(const char *label,
                            const sc_result_t *result);
 
                            
#endif 