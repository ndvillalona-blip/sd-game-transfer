# SD Game Transfer

Homebrew para **Nintendo 2DS / 3DS** que copia, mueve y respalda archivos de la
tarjeta SD con una interfaz animada, cola de trabajos, tiempo estimado y
**deteccion de los juegos del HOME** para respaldar sus partidas guardadas.

Creado por **frisk**.

![icono](icon.png)

## Qué hace

- **Explorador de la SD** con navegación por carpetas y selección múltiple.
- **Cola de transferencia**: marcas varios elementos y se copian **de uno en
  uno**, con progreso, velocidad (MB/s) y tiempo restante.
- **Copiar o mover** (al mover, borra el original solo si la copia termino bien).
- **Deteccion de juegos**: lista los titulos instalados que aparecen en el HOME
  (con su codigo de producto y tamano) y el cartucho insertado, y permite
  respaldar o restaurar sus partidas (archivos de guardado reales, no solo el
  `.sav` de un cartucho).
- **Espacio en vivo** de la tarjeta SD y de la memoria interna (NAND) de la
  consola.
- **No se suspende al cerrar la tapa**: mientras hay una transferencia activa se
  bloquea el modo reposo, asi que puedes cerrar la consola y la copia sigue.
- Interfaz a 60 fps con citro2d: degradados, tarjetas de almacenamiento, barras
  animadas, cola con estados y fuente del sistema (sin assets externos).

## Controles

### Archivos

| Boton | Accion |
|---|---|
| Arriba / Abajo | Mover el cursor |
| L / R | Saltar 4 elementos |
| A | Marcar / desmarcar |
| Y | Enviar lo marcado a la cola y empezar |
| X | Cambiar entre COPIAR y MOVER |
| B | Subir de carpeta |
| SELECT | Cambiar a JUEGOS |
| START | Salir |

### Juegos

| Boton | Accion |
|---|---|
| A | Marcar juego |
| Y | Respaldar las partidas marcadas en `sdmc:/SDGameTransfer/saves/<TITULO>/` |
| X | Restaurar (pide confirmacion: sobrescribe la partida actual) |
| B | Volver a archivos |

Cada fila muestra el codigo de producto, si esta **en la SD** o es **CARTUCHO**,
y el tamano del titulo.

## Donde van los archivos

```
sdmc:/SDGameTransfer/copias/...   archivos y carpetas copiados o movidos
sdmc:/SDGameTransfer/saves/<ID>/  respaldo de cada partida
```

## Instalar

1. Copia `sd-game-transfer.3dsx` a `sdmc:/3ds/`.
2. Abrelo desde el **hbmenu**.
3. Para las partidas hace falta **CFW (Luma3DS)**: sin CFW el sistema no deja
   abrir los archivos de guardado.

## Limites importantes

- **Los juegos de 3DS no se pueden pasar a la memoria interna.** Los titulos
  digitales viven en la SD; la NAND (aprox. 1 GB en 2DS/3DS antiguas) es para el
  sistema, DSiWare y GBA VC, y forzar un juego ahi no lo hace arrancar. Por eso
  esta app trabaja sobre la SD y muestra la NAND solo como informacion.
- **Apagar la consola detiene la transferencia.** Cerrar la tapa no: la app
  bloquea el reposo mientras copia. Si apagas o quitas la bateria, el archivo
  que se estaba copiando puede quedar a medias.
- La restauracion de partidas **sobrescribe** la partida actual del titulo.
- Copiar archivos grandes o muchos a la vez tarda: la velocidad real la marca la
  SD, no la app.
- Maximo 1024 archivos por trabajo y 48 trabajos en cola.

## Pruebas

El motor de copia se puede compilar en el PC con `-DSGT_HOST` y probar con un
arbol grande de archivos:

```sh
gcc -O2 -Wall -Wextra -DSGT_HOST -Isource -Itests/stub \
  source/transfer.c tests/test_transfer.c -o tests/test_transfer.exe
./tests/test_transfer.exe
```

La ultima prueba: 867 archivos, 233 MB, copia y verificacion byte a byte,
sobrescritura, mover, borrado del origen y limite de 1024 archivos.

## Compilar

Necesitas devkitPro (devkitARM + libctru + citro2d, ya incluidos en `3ds-dev`).

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
make
# -> sd-game-transfer.3dsx
```

En este PC hay un toolchain portatil en `../tools/devkitpro`:

```sh
../tools/devkitpro/msys64/msys64/usr/bin/bash.exe -lc \
  'export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH; cd /c/Users/Frisk/Desktop/2ds/sd-game-transfer; make'
```

## Como esta hecho

- `source/main.c`: navegacion, cola, entrada y ciclo principal.
- `source/transfer.c`: recorrido de carpetas y copia por trozos de 256 KB.
- `source/saves.c`: deteccion de titulos (SD y cartucho) y archivos de
  guardado via `ARCHIVE_USER_SAVEDATA` / `ARCHIVE_GAMECARD_SAVEDATA`.
- `source/ui.c`: toda la interfaz con citro2d.
- La copia avanza un trozo por frame, asi la consola sigue respondiendo y el
  progreso/ETA se actualizan sin hilos.

## Creditos

- **frisk** - idea, diseno y desarrollo.
- devkitPro / libctru / citro2d - toolchain y librerias.
- Comunidad de homebrew de 3DS.

## Licencia

MIT. Ver [LICENSE](LICENSE).
