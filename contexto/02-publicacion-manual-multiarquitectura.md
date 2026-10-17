# Publicación manual de Open Neo TTS — octubre 2026

- Se retira el workflow automático .github/workflows/release.yml.
- Se crea .github/workflows/manual-release.yml, con solo workflow_dispatch.
- Tres artefactos: Windows x64, Linux x64, Linux ARM64 nativo (runner ubuntu-24.04-arm).
- Tags tipo 1.0, 1.1, 1.2, etc. Entrada desde_version para saltar directamente a una versión más alta.
- Prohibidos los tags con prefijo v y las versiones duplicadas.
- Se calcula el siguiente número usando Releases y tags publicados en GitHub.
- Versionado testeado en scripts/test-release-version.py.
- Una Release se publica únicamente cuando se compilan las tres plataformas y pasan las pruebas.
- El paquete Linux incluye ONNX Runtime nativo y RPATH relativo; depende de eSpeak, JSON-C y OpenSSL de sistema.
- Se elimina explícitamente la Release v0.1.0 y su tag anterior.
- El código de entrenamiento y Studio sigue fuera del repositorio público.
