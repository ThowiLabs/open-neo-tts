# ONEO Format

ONEO almacena un modelo ONNX, su JSON de configuración y, desde v2, los metadatos y la portada de la voz. No transforma el grafo ONNX ni cifra pesos. Usa enteros *little-endian* e integridad SHA-256.

## ONEO v2

| Offset | Tamaño | Campo |
| --- | ---: | --- |
| 0 | 8 bytes | ASCII `ONEOTTS2` |
| 8 | 4 bytes | Versión `2` |
| 12 | 4 bytes | Tamaño de JSON |
| 16 | 8 bytes | Tamaño de ONNX |
| 24 | 4 bytes | Tamaño de ficha JSON |
| 28 | 4 bytes | Tamaño de portada |
| 32 | 32 bytes | SHA-256 |
| 64 | Variable | Configuración JSON, ficha, portada y ONNX |

La ficha contiene `name`, `author`, `description`, `license`, `version`, `url` y `language`. Los metadatos son opcionales y quedan fijados al compilar. El idioma se obtiene de `espeak.voice` cuando está disponible.

Límites: ONNX 1 GiB; configuración 1 MiB; ficha 2 MiB; portada 8 MiB (PNG, JPEG y WebP).

El SHA-256 cubre la concatenación de:

1. `OPEN-NEO-TTS-ONEO-2`, incluido su byte NUL.
2. Los primeros 32 bytes de la cabecera.
3. Los bytes posteriores al offset 64.

El hash comprueba integridad, no acredita la identidad del autor.

## ONEO v1

El lector mantiene compatibilidad con el formato anterior `ONEOTTS1`. Su cabecera de 56 bytes incluye versión (offset 8), tamaño JSON (offset 12), tamaño ONNX (offset 16) y hash (offset 24). Siguen JSON y ONNX. El SHA-256 v1 cubre `OPEN-NEO-TTS-ONEO-1` más NUL, los primeros 24 bytes y el payload.

ONEO v1 no tiene metadatos ni portada. El compilador actual crea v2, sin herramientas de edición.

## CLI

~~~bash
oneo-compiler build --source voz.onnx --out voz.oneo \
  --name "Mi voz" --author "Desarrollador" --cover portada.png
oneo-compiler verify --model voz.oneo
open-neo-lite infer --model voz.oneo --text "Hola" --out voz.wav
~~~
