/*
 * Flast — a bit-perfect FLAC player for Android.
 * Copyright (C) 2026 Aleatropy and the Flast contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Bundles libFLAC (decoder only), BSD-3-Clause, (C) Xiph.Org Foundation —
 * see app/src/main/cpp/third_party/flac-1.4.3/COPYING.Xiph
 */

#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "playback_controller.h"

#define LOG_TAG "FlacPlayerNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------
// What used to be here, and why it is gone.
//
// This file used to carry ten JNI round-trips for the music library and
// playlists -- jni_scan_music, jni_load_cache, jni_load_playlist,
// jni_save_playlist, jni_list_playlists, jni_create_auto_playlist,
// jni_delete_playlist, jni_add_to_playlist -- plus the two helpers that
// marshalled String[] <-> char** for them.
//
// That marshalling was expensive in exactly the resource this project
// cares about. Handing a 3,000-track library across JNI meant: a Kotlin
// List<String>, a String[] copied from it, 3,000 live java.lang.String
// objects on the ART heap, 3,000 GetStringUTFChars() temporaries, and
// 3,000 strdup()s on the native side -- four representations of the same
// text alive at the same moment, which is what the reported
// "Dalvik Heap size 26,516 KB" spike was made of.
//
// The specification always said this belonged in C (section 3.1:
// "Escaneo de sistema de archivos (POSIX directo, opendir/readdir
// recursivo -- no File API de Java)"). It now is; see music_library.c.
//
// What is left is the genuinely unavoidable Java surface from spec
// section 3.0: things with no NDK equivalent.
// ---------------------------------------------------------------------

static JNIEnv *get_env(struct android_app *app);
static jmethodID get_method(JNIEnv *env, jobject activity, const char *name, const char *sig);

void native_bridge_notify_finished(void) {
    pc_on_track_finished();
}

JNIEXPORT void JNICALL
Java_com_flast_FlastNativeActivity_jniSetDacBlocked(JNIEnv *env, jobject thiz, jboolean blocked) {
    (void)env;
    (void)thiz;
    LOGI("jniSetDacBlocked(%d)", (int)blocked);
    pc_set_dac_blocked(blocked == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_flast_FlastNativeActivity_jniPauseForDeviceChange(JNIEnv *env, jobject thiz) {
    (void)env;
    (void)thiz;
    LOGI("jniPauseForDeviceChange: pausando (posicion preservada)");
    pc_pause_for_device_change();
}

// Set from the UI thread (Kotlin, via jniRescanMusic()) once storage
// permission is confirmed granted -- the very first scan can otherwise
// race the permission dialog, run with no permission, and cache an
// empty result with nothing to ever retry it. android_main()'s loop
// polls native_bridge_consume_rescan_request() every iteration and
// kicks off a background rescan when this comes back true.
static atomic_bool g_rescan_requested = false;

bool native_bridge_consume_rescan_request(void) {
    bool expected = true;
    return atomic_compare_exchange_strong(&g_rescan_requested, &expected, false);
}

// ---------------------------------------------------------------------
// The DAC volume question (spec 3.5) used to be an android.app.AlertDialog
// put up from handleDacAttached(). Two problems with that:
//
//   1. If the user had swiped the app away while playback continued in
//      the foreground service, and THEN plugged in a new DAC, that
//      AlertDialog.Builder(this).show() ran against a destroyed Activity
//      and threw WindowManager$BadTokenException -- a crash, on the
//      hearing-safety path, in exactly the situation the question exists
//      for.
//   2. It was the app's only real View, which is what forced
//      hardwareAccelerated="true" and its GPU context on the process.
//
// The question is now a native screen. Kotlin still owns the *decision*
// (persisting it, and applying STREAM_MUSIC); this flag is only "is a
// question outstanding", which the UI thread polls each loop.
// ---------------------------------------------------------------------
static atomic_bool g_dac_question_pending = false;

bool native_bridge_dac_question_pending(void) {
    return atomic_load(&g_dac_question_pending);
}

JNIEXPORT void JNICALL
Java_com_flast_FlastNativeActivity_jniSetDacQuestionPending(JNIEnv *env, jobject thiz,
                                                             jboolean pending) {
    (void)env;
    (void)thiz;
    LOGI("jniSetDacQuestionPending(%d)", (int)pending);
    atomic_store(&g_dac_question_pending, pending == JNI_TRUE);
}

// Called from the UI thread when the user taps YES or NO on that screen.
// Hands the answer to Kotlin, which persists it per vendor/product ID and
// applies the volume policy, then clears the flag so the screen goes away.
void native_bridge_answer_dac_question(struct android_app *app, bool has_own_control) {
    JNIEnv *env = get_env(app);
    if (env != NULL) {
        jobject activity = app->activity->clazz;
        jmethodID mid = get_method(env, activity, "jniAnswerDacVolume", "(Z)V");
        if (mid != NULL) {
            (*env)->CallVoidMethod(env, activity, mid,
                                   (jboolean)(has_own_control ? JNI_TRUE : JNI_FALSE));
        }
    }
    atomic_store(&g_dac_question_pending, false);
}

// Lets the empty-library message tell the truth. Without it a user who
// taps "Don't allow" is told "(empty -- no .flac files found)", which
// blames their music collection for a permission they just declined.
static atomic_bool g_storage_permission = false;

bool native_bridge_has_storage_permission(void) {
    return atomic_load(&g_storage_permission);
}

JNIEXPORT void JNICALL
Java_com_flast_FlastNativeActivity_jniSetStoragePermission(JNIEnv *env, jobject thiz,
                                                            jboolean granted) {
    (void)env;
    (void)thiz;
    atomic_store(&g_storage_permission, granted == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_flast_FlastNativeActivity_jniRescanMusic(JNIEnv *env, jobject thiz) {
    (void)env;
    (void)thiz;
    LOGI("jniRescanMusic: solicitando re-escaneo de musica");
    atomic_store(&g_rescan_requested, true);
}

// IMPORTANT: android_main() runs on a NEW native thread every time the
// Activity is (re)created (e.g. after the user swipes the app away and
// reopens it) -- android_native_app_glue spawns a fresh pthread per
// ANativeActivity instance. A JNIEnv* is only ever valid on the thread
// that attached it, so this cache must NOT survive across android_main
// invocations, and the thread must be detached before it dies. Getting
// this wrong is what used to crash the app on reopen: a stale JNIEnv*
// from an already-dead thread was being reused on the new thread.
static JNIEnv *g_cached_env = NULL;
static bool g_cached_env_valid = false;

// Call once at the very top of android_main(), before any JNI call on
// this (new) thread.
void native_bridge_reset_env(void) {
    g_cached_env = NULL;
    g_cached_env_valid = false;
}

// Call once right before android_main() returns / this thread exits.
void native_bridge_detach_env(struct android_app *app) {
    if (g_cached_env_valid) {
        JavaVM *vm = app->activity->vm;
        (*vm)->DetachCurrentThread(vm);
        g_cached_env = NULL;
        g_cached_env_valid = false;
    }
}

static JNIEnv *get_env(struct android_app *app) {
    if (g_cached_env_valid) {
        return g_cached_env;
    }
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    jint result = (*vm)->AttachCurrentThread(vm, &env, NULL);
    if (result != JNI_OK || env == NULL) {
        LOGE("get_env: AttachCurrentThread fallo (result=%d) -- las llamadas JNI hacia "
             "la capa Kotlin van a fallar", result);
        return NULL;
    }
    g_cached_env = env;
    g_cached_env_valid = true;
    return g_cached_env;
}

static jmethodID get_method(JNIEnv *env, jobject activity, const char *name, const char *sig) {
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID mid = (*env)->GetMethodID(env, cls, name, sig);
    (*env)->DeleteLocalRef(env, cls);
    if (mid == NULL) {
        LOGE("get_method: no se encontro %s%s -- revisar FlastNativeActivity.kt", name, sig);
        (*env)->ExceptionClear(env);
    }
    return mid;
}

int jni_get_bit_perfect_state(struct android_app *app, bool is_bit_perfect_native) {
    JNIEnv *env = get_env(app);
    if (env == NULL) return 2;
    jobject activity = app->activity->clazz;
    jmethodID mid = get_method(env, activity, "jniGetBitPerfectStateForNative", "(Z)I");
    if (mid == NULL) return 2;
    return (int)(*env)->CallIntMethod(env, activity, mid, (jboolean)(is_bit_perfect_native ? JNI_TRUE : JNI_FALSE));
}

char *jni_get_bit_perfect_reason(struct android_app *app, bool is_bit_perfect_native) {
    JNIEnv *env = get_env(app);
    if (env == NULL) return NULL;
    jobject activity = app->activity->clazz;
    jmethodID mid = get_method(env, activity, "jniGetBitPerfectReason", "(Z)Ljava/lang/String;");
    if (mid == NULL) return NULL;
    jstring result = (jstring)(*env)->CallObjectMethod(env, activity, mid, (jboolean)(is_bit_perfect_native ? JNI_TRUE : JNI_FALSE));
    if (result == NULL) return NULL;
    const char *utf = (*env)->GetStringUTFChars(env, result, NULL);
    char *out = utf != NULL ? strdup(utf) : NULL;
    if (utf != NULL) (*env)->ReleaseStringUTFChars(env, result, utf);
    (*env)->DeleteLocalRef(env, result);
    return out;
}

// Asks the framework which storage volumes exist. One JNI call, made
// once per scan, returning a handful of short strings -- nothing like
// the per-track String[] marshalling this file used to do.
int jni_get_storage_roots(struct android_app *app, char ***out) {
    *out = NULL;
    JNIEnv *env = get_env(app);
    if (env == NULL) return 0;
    jobject activity = app->activity->clazz;
    jmethodID mid = get_method(env, activity, "jniStorageRoots", "()[Ljava/lang/String;");
    if (mid == NULL) return 0;

    jobjectArray arr = (jobjectArray)(*env)->CallObjectMethod(env, activity, mid);
    if (arr == NULL) return 0;
    jsize n = (*env)->GetArrayLength(env, arr);
    if (n <= 0) { (*env)->DeleteLocalRef(env, arr); return 0; }

    char **result = (char **)malloc(sizeof(char *) * (size_t)n);
    if (result == NULL) { (*env)->DeleteLocalRef(env, arr); return 0; }

    int w = 0;
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring)(*env)->GetObjectArrayElement(env, arr, i);
        if (js == NULL) continue;
        const char *utf = (*env)->GetStringUTFChars(env, js, NULL);
        if (utf != NULL) {
            result[w] = strdup(utf);
            if (result[w] != NULL) w++;
            (*env)->ReleaseStringUTFChars(env, js, utf);
        }
        (*env)->DeleteLocalRef(env, js);
    }
    (*env)->DeleteLocalRef(env, arr);
    *out = result;
    return w;
}

void jni_free_storage_roots(char **roots, int count) {
    if (roots == NULL) return;
    for (int i = 0; i < count; i++) free(roots[i]);
    free(roots);
}

// Fallback for the (rare, but real on some ROMs) case where
// ANativeActivity::internalDataPath comes back NULL. Called at most once
// per process, only on that path.
bool jni_get_files_dir(struct android_app *app, char *out, size_t out_sz) {
    if (out == NULL || out_sz == 0) return false;
    out[0] = '\0';

    JNIEnv *env = get_env(app);
    if (env == NULL) return false;
    jobject activity = app->activity->clazz;

    jmethodID mid = get_method(env, activity, "jniFilesDir", "()Ljava/lang/String;");
    if (mid == NULL) return false;

    jstring result = (jstring)(*env)->CallObjectMethod(env, activity, mid);
    if (result == NULL) return false;

    const char *utf = (*env)->GetStringUTFChars(env, result, NULL);
    bool ok = false;
    if (utf != NULL && utf[0] != '\0') {
        size_t n = strlen(utf);
        if (n >= out_sz) n = out_sz - 1;
        memcpy(out, utf, n);
        out[n] = '\0';
        ok = true;
    }
    if (utf != NULL) (*env)->ReleaseStringUTFChars(env, result, utf);
    (*env)->DeleteLocalRef(env, result);
    return ok;
}
