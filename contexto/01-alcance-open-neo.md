# Fecha
17 de octubre de 2026

# Objetivo
Edición pública mínima con Releases automáticas.

# Decisiones tomadas
El fork abierto ofrece solo oneo-compiler y open-neo-lite. Entrenamiento y Studio permanecen privados. ONEO v2 incluye metadatos y portada; los archivos v1 siguen funcionando. No existe editor ONEO.

# Arquitectura actual
Motor C++17 con ONNX Runtime y eSpeak-NG. Workflow compila Linux y Windows y publica tags de versiones incrementales.

# Librerías usadas
C++17, OpenSSL, JSON-C, ONNX Runtime, eSpeak-NG. Python solo para pruebas y empaquetado de Windows.

# Archivos importantes modificados
CMakeLists.txt, src/open_format, .github/workflows/release.yml, scripts/, README.md y docs/.

# Problemas encontrados
La versión pública anterior contenía fuentes de entrenamiento y Studio; se retiraron. La primera ejecución Windows detectó una pérdida de UTF-8 en argumentos, corregida al usar wmain. Después los dos EXE compilaron y pasó la batería de pruebas. El empaquetado requirió resolver la ruta UCRT64 para copiar DLLs.

# Soluciones implementadas
Dos ejecutables, ONEO v2 compatible con v1 y publicación condicionada a compilaciones y pruebas correctas.

# Pendientes
Verificación del primer release y licencias de las DLL redistribuidas.

# Próximos pasos
Validar paquetes de descarga y, en futuras versiones, valorar entrenamiento abierto.
