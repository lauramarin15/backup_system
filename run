#!/bin/bash

# Hacer que el script se detenga si hay un error
set -e

echo "=================================="
echo "   COMPILACIÓN Y PRUEBA MANUAL    "
echo "=================================="

echo "[1/3] Compilando el programa..."
gcc -Wall -Wextra -o backup_EAFITos backup.c
echo "¡Compilación exitosa!"

echo ""
echo "[2/3] Mostrando ayuda del CLI..."
./backup_EAFITos --help

echo ""
echo "[3/3] Quieres ejecutar una prueba rápida? (s/n)"
read -r respuesta

if [[ "$respuesta" == "s" || "$respuesta" == "S" ]]; then
    echo "Creando test_dir y ejecutando backup..."
    mkdir -p test_dir
    echo "Prueba desde bash" > test_dir/test.txt
    
    ./backup_cli -b test_dir test_dir_backup
    
    echo "Contenido de test_dir_backup:"
    ls -l test_dir_backup
fi
