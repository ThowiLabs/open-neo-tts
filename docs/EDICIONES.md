# Open Neo TTS y Neo TTS

Open Neo TTS es el fork abierto del proyecto privado [Neo TTS](https://github.com/ThowiLabs/neo-tts) de ThowiLabs.

Este repositorio ofrece dos ejecutables: `oneo-compiler` y `open-neo-lite`. No distribuye el entrenador VITS, Studio ni el compilador y formato NEO privativos.

| Función | Open Neo TTS | Neo TTS |
| --- | --- | --- |
| Inferencia ONNX | Sí | Sí |
| Inferencia ONEO | Sí | Sí |
| Compilación ONEO | Sí | Sí |
| Compilación e inferencia NEO | No | Sí |
| Entrenamiento VITS y Studio | No | Sí |

ONEO permite definir nombre, autor, descripción, portada, licencia, versión y web al compilar. No tiene un editor posterior. El entrenamiento podrá añadirse en el futuro, pero no forma parte de la edición pública actual.

El repositorio abierto se mantiene separado del privado y revisa que no incorpore módulos del formato NEO.
