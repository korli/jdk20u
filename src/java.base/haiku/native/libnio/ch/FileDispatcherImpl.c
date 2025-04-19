/*
 * Copyright (c) 2000, 2025, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "jni.h"
#include "nio.h"
#include "nio_util.h"
#include "sun_nio_ch_FileDispatcherImpl.h"

static jfieldID fd_fdID; // ID for FileDescriptor.fd field

#define CHECK_NULL(x) if ((x) == NULL) { \
    fprintf(stderr, "%s: null pointer\n", __func__); \
    return; \
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_FileDispatcherImpl_init(JNIEnv *env, jclass clazz) {
    fprintf(stderr, "UnixFileDispatcherImpl_init: registering natives\n");
    CHECK_NULL(LOAD_FIELD_ID(fd_fdID, clazz, "fd", "Ljava/io/FileDescriptor;"));
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_read0(JNIEnv *env, jclass clazz,
                                         jobject fdo, jlong address, jint len) {
    fprintf(stderr, "UnixFileDispatcherImpl_read0: fd=%p, address=%lld, len=%d\n",
            fdo, (long long)address, len);
    jint fd = fdval(env, fdo);
    void *buf = (void *)jlong_to_ptr(address);
    ssize_t result;

    result = read(fd, buf, len);
    if (result == -1) {
        fprintf(stderr, "UnixFileDispatcherImpl_read0: read failed: %s\n", strerror(errno));
        JNU_ThrowIOExceptionWithLastError(env, "Read failed");
        return -1;
    }
    fprintf(stderr, "UnixFileDispatcherImpl_read0: read %zd bytes\n", result);
    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_write0(JNIEnv *env, jclass clazz,
                                          jobject fdo, jlong address, jint len) {
    fprintf(stderr, "UnixFileDispatcherImpl_write0: fd=%p, address=%lld, len=%d\n",
            fdo, (long long)address, len);
    jint fd = fdval(env, fdo);
    void *buf = (void *)jlong_to_ptr(address);
    ssize_t result;

    result = write(fd, buf, len);
    if (result == -1) {
        fprintf(stderr, "UnixFileDispatcherImpl_write0: write failed: %s\n", strerror(errno));
        JNU_ThrowIOExceptionWithLastError(env, "Write failed");
        return -1;
    }
    fprintf(stderr, "UnixFileDispatcherImpl_write0: wrote %zd bytes\n", result);
    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_force0(JNIEnv *env, jclass clazz,
                                          jobject fdo, jboolean metaData) {
    fprintf(stderr, "UnixFileDispatcherImpl_force0: fd=%p, metaData=%d\n",
            fdo, metaData);
    jint fd = fdval(env, fdo);
    int result;

    // On Haiku, fsync() is sufficient for both data and metadata
    result = fsync(fd);
    if (result == -1) {
        fprintf(stderr, "UnixFileDispatcherImpl_force0: fsync failed: %s\n", strerror(errno));
        JNU_ThrowIOExceptionWithLastError(env, "Force failed");
        return -1;
    }
    fprintf(stderr, "UnixFileDispatcherImpl_force0: fsync succeeded\n");
    return 0;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_FileDispatcherImpl_transferTo0(JNIEnv *env, jobject this,
                                               jobject srcFDO, jlong position,
                                               jlong count, jobject dstFDO,
                                               jboolean append) {
    fprintf(stderr, "UnixFileDispatcherImpl_transferTo0: srcFD=%p, pos=%lld, count=%lld, dstFD=%p, append=%d\n",
            srcFDO, (long long)position, (long long)count, dstFDO, append);

    // Haiku doesn't support sendfile or copy_file_range, so use read/write
    if (append == JNI_TRUE) {
        fprintf(stderr, "UnixFileDispatcherImpl_transferTo0: append not supported\n");
        return IOS_UNSUPPORTED_CASE;
    }

    jint srcFD = fdval(env, srcFDO);
    jint dstFD = fdval(env, dstFDO);

    // Seek to position in source file
    if (lseek(srcFD, (off_t)position, SEEK_SET) == -1) {
        fprintf(stderr, "UnixFileDispatcherImpl_transferTo0: lseek failed: %s\n", strerror(errno));
        JNU_ThrowIOExceptionWithLastError(env, "Seek failed");
        return IOS_THROWN;
    }

    // Buffer for transfer
    char buf[8192];
    jlong total_transferred = 0;
    jlong remaining = count;

    while (remaining > 0) {
        size_t to_read = (remaining > (jlong)sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
        ssize_t nread = read(srcFD, buf, to_read);
        if (nread == -1) {
            if (errno == EINTR) {
                fprintf(stderr, "UnixFileDispatcherImpl_transferTo0: read interrupted\n");
                return IOS_INTERRUPTED;
            }
            fprintf(stderr, "UnixFileDispatcherImpl_transferTo0: read failed: %s\n", strerror(errno));
            JNU_ThrowIOExceptionWithLastError(env, "Read failed");
            return IOS_THROWN;
        }
        if (nread == 0) {
            break; // EOF
        }

        ssize_t nwritten = write(dstFD, buf, nread);
        if (nwritten == -1) {
            if (errno == EINTR) {
                fprintf(stderr, "UnixFileDispatcherImpl_transferTo0: write interrupted\n");
                return IOS_INTERRUPTED;
            }
            fprintf(stderr, "UnixFileDispatcherImpl_transferTo0: write failed: %s\n", strerror(errno));
            JNU_ThrowIOExceptionWithLastError(env, "Write failed");
            return IOS_THROWN;
        }

        total_transferred += nwritten;
        remaining -= nwritten;
    }

    fprintf(stderr, "UnixFileDispatcherImpl_transferTo0: transferred %lld bytes\n", (long long)total_transferred);
    return total_transferred;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_FileDispatcherImpl_transferFrom0(JNIEnv *env, jobject this,
                                                 jobject srcFDO, jobject dstFDO,
                                                 jlong position, jlong count,
                                                 jboolean append) {
    fprintf(stderr, "UnixFileDispatcherImpl_transferFrom0: srcFD=%p, dstFD=%p, pos=%lld, count=%lld, append=%d\n",
            srcFDO, dstFDO, (long long)position, (long long)count, append);

    // Same implementation as transferTo0, as Haiku doesn't distinguish direction
    return Java_sun_nio_ch_FileDispatcherImpl_transferTo0(env, this, srcFDO, position, count, dstFDO, append);
}