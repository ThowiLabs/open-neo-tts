# Open Neo TTS

Open Neo TTS es el fork abierto de [Neo TTS](https://github.com/ThowiLabs/neo-tts), desarrollado por ThowiLabs. Contiene únicamente el compilador del formato ONEO y el motor ligero de inferencia. El entrenamiento y Studio no forman parte de esta distribución.

## Herramientas

- **oneo-compiler**: empaqueta un modelo ONNX y su configuración en un archivo `.oneo`, con nombre, autor, descripción, portada, licencia, versión y URL.
- **open-neo-lite**: genera voz a partir de `.onnx` o `.oneo` mediante ONNX Runtime y eSpeak-NG.

Los modelos `.neo` y su compilador siguen siendo exclusivos del proyecto privado. ONEO es un contenedor abierto: conserva los pesos ONNX originales y no los cifra.

## Descargas

Los paquetes para **Windows x64, Linux x64 y Linux ARM64** se publican mediante GitHub Actions en [Releases](https://github.com/ThowiLabs/open-neo-tts/releases). Los paquetes contienen sus ejecutables, las bibliotecas indicadas y un archivo `SHA256SUMS.txt`.

En Linux, instala eSpeak-NG, sus datos de idioma, JSON-C y OpenSSL; el paquete incluye ONNX Runtime. En Windows, conserva las DLL y `espeak-ng-data` junto a los ejecutables.

## Compilar

Necesitas CMake 3.18+, C++17, OpenSSL, JSON-C, eSpeak-NG y ONNX Runtime.

~~~bash
cmake -S . -B build -DNEO_ORT_INCLUDE=/ruta/onnxruntime/include \
  -DNEO_ORT_LIBRARY=/ruta/onnxruntime/lib/libonnxruntime.so
cmake --build build -j2
ctest --test-dir build --output-on-failure
~~~

Se generan únicamente `oneo-compiler` y `open-neo-lite`. Python solo se usa en pruebas, no en tiempo de inferencia.

## Crear una voz ONEO

Coloca el archivo de configuración junto al modelo, por ejemplo `voz.onnx.json`. Los datos de la voz se establecen al compilar.

~~~bash
./build/oneo-compiler build --source voz.onnx --out voz.oneo \
  --name "Mi voz" --author "Autor" \
  --description "Voz en español" --cover portada.png \
  --license "CC BY 4.0" --version "1.0.0" \
  --url "https://example.com"

./build/oneo-compiler verify --model voz.oneo
~~~

Todos los metadatos son opcionales. No hay editor de archivos ONEO. La portada admite PNG, JPEG o WebP hasta 8 MiB.

## Inferencia

~~~bash
./build/open-neo-lite infer --model voz.oneo \
  --text "Hola desde Open Neo TTS" --out voz.wav

./build/open-neo-lite infer --model voz.onnx \
  --text-file texto.txt --out voz-onnx.wav
~~~

ONNX requiere el archivo JSON externo; ONEO incluye ambos.

## Publicar una versión

El [workflow de Releases](.github/workflows/manual-release.yml) **solo puede ejecutarse manualmente** en GitHub: Actions → Publicar Open Neo TTS → Run workflow, con la rama `main` seleccionada.

Si no existe ninguna Release, se crea el tag `1.0`. Cada ejecución posterior incrementa el segundo número, por ejemplo `1.1`, `1.2` y `1.3`, sin prefijo `v`.

La entrada opcional `desde_version` permite fijar la próxima versión exacta, por ejemplo `3.0`. Tiene que superar el último tag numérico existente. Una ejecución posterior con el campo vacío avanzará a `3.1`.

Se compilan `oneo-compiler` y `open-neo-lite` para Windows x64, Linux x64 y Linux ARM64, con pruebas para las tres plataformas. Cuando todas pasan, se crea la Release con los tres paquetes y `SHA256SUMS.txt`. Los commits y pull requests **no publican** versiones.

Los nombres de los paquetes tienen el formato `open-neo-tts-linux-x64-1.0.tar.gz`, `open-neo-tts-linux-arm64-1.0.tar.gz` y `open-neo-tts-windows-x64-1.0.zip`.

Consulta la [especificación ONEO](docs/ONEO_FORMAT.md) y la [división de ediciones](docs/EDICIONES.md).

## Licencia

El código propio se distribuye bajo [MIT](LICENSE). Las dependencias y los modelos mantienen sus licencias originales. eSpeak-NG usa GPL-3.0-o-posterior; revisa sus obligaciones antes de redistribuir paquetes con sus bibliotecas.
