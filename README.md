# Newliveaudioapp

## Solución de compilación: Java 17

Este proyecto Android requiere Java 17 para funcionar correctamente con Gradle 8.2 y el plugin de Android 8.2.0.

El problema real era que el entorno estaba usando Java 25.0.2, que no es compatible con esta configuración, y Gradle fallaba antes de compilar.

### Qué se dejó configurado

Se añadió la ruta de Java 17 en [gradle.properties](gradle.properties):

```
org.gradle.java.home=/usr/lib/jvm/java-17-openjdk-amd64
```

Esto fuerza que Gradle use el JDK correcto aunque el sistema tenga otra versión activa.

### Verificación

Se comprobó con:

```
./gradlew assembleDebug --console=plain
```

Resultado: compilación correcta con `BUILD SUCCESSFUL`.

### Si vuelves a tener el problema

Ejecuta esto en la terminal antes de compilar:

```
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
./gradlew assembleDebug
```

La versión correcta instalada en este entorno es Java 17, no Java 25.
