# Open Neo TTS

Open Neo TTS es el fork abierto de [Neo TTS](https://github.com/ThowiLabs/neo-tts), desarrollado por ThowiLabs. Contiene únicamente el compilador del formato ONEO y el motor ligero de inferencia. El entrenamiento y Studio no forman parte de esta distribución.

## Herramientas

- **oneo-compiler**: empaqueta un modelo ONNX y su configuración en un archivo `.oneo`, con nombre, autor, descripción, portada, licencia, versión y URL.
- **open-neo-lite**: genera voz a partir de `.onnx` o `.oneo` mediante ONNX Runtime y eSpeak-NG.

Los modelos `.neo` y su compilador siguen siendo exclusivos del proyecto privado. ONEO es un contenedor abierto: conserva los pesos ONNX originales y no los cifra.

## Descargas

Los binarios para Linux x64 y Windows x64 se publican mediante GitHub Actions en [Releases](https://github.com/ThowiLabs/open-neo-tts/releases). Los paquetes contienen sus ejecutables, las bibliotecas indicadas y un archivo `SHA256SUMS.txt`.

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

## Versiones

El [workflow de publicación](.github/workflows/release.yml) compila Linux y Windows, pasa las pruebas y crea una Release cuando ambas plataformas finalizan correctamente. Comienza con `v0.1.0`; después incrementa automáticamente la versión *patch* (`v0.1.1`, `v0.1.2`, etc.). Desde **Run workflow** también permite elegir `minor` o `major`.

Los cambios en `main` activan la publicación y las pull requests solamente compilan y prueban. Si un build falla, no se crea Release.

Consulta la [especificación ONEO](docs/ONEO_FORMAT.md) y la [división de ediciones](docs/EDICIONES.md).

## Licencia

El código propio se distribuye bajo [MIT](LICENSE). Las dependencias y los modelos mantienen sus licencias originales. eSpeak-NG usa GPL-3.0-o-posterior; revisa sus obligaciones antes de redistribuir paquetes con sus bibliotecas.
