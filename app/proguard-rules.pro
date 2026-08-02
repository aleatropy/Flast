# proguard-rules.pro
#
# Vacío deliberadamente por ahora: este proyecto no tiene dependencias
# de terceros (androidx, Compose, etc.) que requieran reglas de
# conservación específicas de ProGuard/R8. Si en pasos futuros se
# introduce alguna librería (por ejemplo, si el motor gráfico del paso 5
# terminara necesitando algo puntual), las reglas correspondientes se
# añaden aquí, no antes de que exista una necesidad real.

# Mantener nombres de métodos nativos JNI legibles en stack traces de
# depuración -- esto sí es útil desde ya, dado que la capa nativa es el
# corazón del proyecto y los crashes ahí son los que más importa
# diagnosticar bien.
-keepclasseswithmembernames class * {
    native <methods>;
}

# FlastNativeActivity's jni* methods are called from C via
# GetMethodID() by name/signature -- there is no static Java-side call
# site R8 can see, so without this rule a minified release build is
# free to rename or strip them. That's silent: the debug build (no
# minification) keeps working, and only the signed release build
# breaks, with native_bridge.c's get_method() logging a "method not
# found" error and every native<->Kotlin call quietly failing.
#
# The eight library/playlist methods that used to be listed here are
# gone with the Kotlin MusicLibrary -- that work happens in
# music_library.c now, with no JNI crossing at all. What is left is the
# bit-perfect state the framework owns (AudioManager volume, Bluetooth
# routing, the DAC answer) plus the internalDataPath fallback.
-keepclassmembers class com.flast.FlastNativeActivity {
    int jniGetBitPerfectStateForNative(int);
    java.lang.String jniGetBitPerfectReason(int);
    java.lang.String jniFilesDir();
    void jniAnswerDacVolume(boolean);
    java.lang.String[] jniStorageRoots();
}

# R8's own optimiser is allowed to be aggressive here: nothing in this
# app is a public API, nothing is reflected over, and nothing is
# serialised. These two lines are what let it inline and merge the
# small Kotlin objects (DacVolumePrefs, UsbDacDetector.Companion)
# instead of keeping each one as its own class in classes.dex.
-allowaccessmodification
-repackageclasses ''
