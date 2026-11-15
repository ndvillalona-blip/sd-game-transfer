# SD Game Transfer

Homebrew para **Nintendo 2DS / 3DS** que copia, mueve y respalda archivos de la
tarjeta SD con una interfaz animada, cola de trabajos, tiempo estimado y
transferencia de **partidas guardadas**.

Creado por **frisk**.

![icono](icon.png)

## Qué hace

- **Explorador de la SD** con navegación por carpetas y selección múltiple.
- **Cola de transferencia**: marcas varios elementos y se copian **de uno en
  uno**, con progreso, velocidad (MB/s) y tiempo restante.
- **Copiar o mover** (al mover, borra el original solo si la copia termino bien).
- **Respaldo y restauración de partidas** de los títulos instalados en la SD
  (archivos de guardado reales, no solo el `.sav` de un cartucho).
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
| SELECT | Cambiar a PARTIDAS |
| START | Salir |

### Partidas

| Boton | Accion |
|---|---|
| A | Marcar titulo |
| Y | Respaldar las partidas marcadas en `sdmc:/SDGameTransfer/saves/<TITULO>/` |
| X | Restaurar (pide confirmacion: sobrescribe la partida actual) |
| B | Volver a archivos |

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
- `source/saves.c`: archivos de guardado via `ARCHIVE_USER_SAVEDATA`.
- `source/ui.c`: toda la interfaz con citro2d.
- La copia avanza un trozo por frame, asi la consola sigue respondiendo y el
  progreso/ETA se actualizan sin hilos.

## Creditos

- **frisk** - idea, diseno y desarrollo.
- devkitPro / libctru / citro2d - toolchain y librerias.
- Comunidad de homebrew de 3DS.

## Licencia

MIT. Ver [LICENSE](LICENSE).
