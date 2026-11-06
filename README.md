# Optimizalis

Optimizador de espacio en disco para Windows, escrito en C++ (Win32 + GDI+), en un único ejecutable sin dependencias.

## Qué hace

- **Inicio**: estado del disco, espacio recuperable, limpieza segura en un clic y estadísticas de todo lo liberado.
- **Resultados**: cada categoría en su tarjeta (temporales, cachés de navegadores, aplicaciones y desarrollo, Windows Update, volcados, miniaturas, Windows.old, papelera, proyectos, archivos grandes, duplicados, descargas y restos). Verde = seguro · Amarillo = revisar · Rojo = personal.
- **Detalle**: lista de elementos con filtro, orden por tamaño o nombre, selección múltiple (Mayús+clic, Ctrl+A) y acceso a la ubicación.
- **Restos de programas**: búsqueda por nombre en todas las unidades y en el registro (configuración, desinstalación, inicio automático). Si el programa sigue instalado, ofrece su desinstalador oficial.
- **Protección**, **Historial** y **Ajustes** (umbrales, color de acento, animaciones y permisos).

## Seguridad

- Nunca borra vídeos, audios ni fotos, ni sueltos ni dentro de carpetas.
- Lo esencial de Windows aparece con candado y no se puede seleccionar; la protección se vuelve a comprobar antes de borrar cada elemento.
- Para borrar hay que escribir `ELIMINAR`. El borrado es permanente (no pasa por la Papelera).
- Cada clave del registro se exporta antes a `copias_registro\` (.reg) para poder restaurarla.
- Todo lo eliminado queda anotado en `registro_eliminaciones.txt`.

## Descarga

El ejecutable está en la sección [Releases](../../releases). Doble clic en `Optimizalis.exe`; se recomienda "Elevar permisos" para limpiar también Windows Update, el Temp de Windows y el registro del sistema.

## Compilar

Requiere [MinGW-w64](https://www.mingw-w64.org/) (`g++` y `windres`) en el PATH:

```bat
codigo_fuente\compilar.bat
```

Genera `Optimizalis.exe` en la carpeta principal.
