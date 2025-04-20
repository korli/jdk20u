/*
 * Copyright (c) 2000, 2022, Oracle and/or its affiliates. All rights reserved.
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

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <assert.h>
//#include <OS.h>

#include "jni.h"
#include "nio.h"
#include "nio_util.h"
#include "sun_nio_ch_UnixFileDispatcherImpl.h"

static jlong
handle(JNIEnv *env, jlong rv, char *msg)
{
    if (rv >= 0)
        return rv;
    if (errno == EINTR)
        return IOS_INTERRUPTED;
    JNU_ThrowIOExceptionWithLastError(env, msg);
    return IOS_THROWN;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_allocationGranularity0(JNIEnv *env, jclass clazz) {
    return (jlong)getpagesize();

    /*
    system_info info;
    if (get_system_info(&info) != B_OK) {
        JNU_ThrowIOExceptionWithLastError(env, "get_system_info failed");
        return 0;
    }
    return (jlong)info.page_size;
    */
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_map0(JNIEnv *env, jclass klass, jobject fdo,
                                        jint prot, jlong off, jlong len,
                                        jboolean map_sync)
{
    void *mapAddress = 0;
    jint fd = fdval(env, fdo);
    int protections = 0;
    int flags = 0;

    fprintf(stderr, "map0: fd=%d, prot=%d, position=%lld, length=%lld, map_sync=%d\n", fd, prot, len, map_sync);


    // should never be called with map_sync and prot == PRIVATE
    assert((prot != sun_nio_ch_UnixFileDispatcherImpl_MAP_PV) || !map_sync);

    if (prot == sun_nio_ch_UnixFileDispatcherImpl_MAP_RO) {
        protections = PROT_READ;
        flags = MAP_SHARED;
    } else if (prot == sun_nio_ch_UnixFileDispatcherImpl_MAP_RW) {
        protections = PROT_WRITE | PROT_READ;
        flags = MAP_SHARED;
    } else if (prot == sun_nio_ch_UnixFileDispatcherImpl_MAP_PV) {
        protections =  PROT_WRITE | PROT_READ;
        flags = MAP_PRIVATE;
    }

    // if MAP_SYNC and MAP_SHARED_VALIDATE are not defined then it is
    // best to define them here. This ensures the code compiles on old
    // OS releases which do not provide the relevant headers. If run
    // on the same machine then it will work if the kernel contains
    // the necessary support otherwise mmap should fail with an
    // invalid argument error

#ifndef MAP_SYNC
#define MAP_SYNC 0x80000
#endif
#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE 0x03
#endif

    if (map_sync) {
        // ensure
        //  1) this is Linux on AArch64, x86_64, or PPC64 LE
        //  2) the mmap APIs are available at compile time
#if !defined(HAIKU) || !defined(LINUX) || ! (defined(aarch64) || (defined(amd64) && defined(_LP64)) || defined(ppc64le))
        // TODO - implement for solaris/AIX/BSD/WINDOWS and for 32 bit
        JNU_ThrowInternalError(env, "should never call map on platform where MAP_SYNC is unimplemented");
        return IOS_THROWN;
#else
        flags |= MAP_SYNC | MAP_SHARED_VALIDATE;
#endif
    }

    mapAddress = mmap(
        0,                    /* Let OS decide location */
        len,                  /* Number of bytes to map */
        protections,          /* File permissions */
        flags,                /* Changes are shared */
        fd,                   /* File descriptor of mapped file */
        off);                 /* Offset into file */

    if (mapAddress == MAP_FAILED) {
        if (map_sync && errno == ENOTSUP) {
            JNU_ThrowIOExceptionWithLastError(env, "map with mode MAP_SYNC unsupported");
            return IOS_THROWN;
        }

        if (errno == ENOMEM) {
            JNU_ThrowOutOfMemoryError(env, "Map failed");
            return IOS_THROWN;
        }
        return handle(env, -1, "Map failed");
    }

    return ((jlong) (unsigned long) mapAddress);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_unmap0(JNIEnv *env, jclass klass,
                                          jlong address, jlong len)
{
    void *a = (void *)jlong_to_ptr(address);
    return handle(env,
                  munmap(a, (size_t)len),
                  "Unmap failed");
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_transferTo0(JNIEnv *env, jobject this,
                                                jobject srcFDO,
                                                jlong position, jlong count,
                                                jobject dstFDO, jboolean append)
{
    jint srcFD = fdval(env, srcFDO);
    jint dstFD = fdval(env, dstFDO);
    jlong transferred = 0;
    ssize_t read_bytes;
    ssize_t written_bytes;
    off_t current_src_pos;
    char buffer[8192];

    current_src_pos = lseek(srcFD, position, SEEK_SET);
    if (current_src_pos == -1) {
        JNU_ThrowIOExceptionWithLastError(env, "Seek on source failed");
        return IOS_THROWN;
    }

    while (transferred < count) {
        long bytes_to_read = (count - transferred < sizeof(buffer)) ? (count - transferred) : sizeof(buffer);

        read_bytes = read(srcFD, buffer, bytes_to_read);

        if (read_bytes > 0) {
            ssize_t total_written = 0;
            char *write_ptr = buffer;

            while (total_written < read_bytes) {
                off_t write_offset = append ? lseek(dstFD, 0, SEEK_END) : lseek(dstFD, 0, SEEK_CUR);
                if (write_offset == -1 && append) {
                    write_offset = lseek(dstFD, 0, SEEK_CUR);
                }
                if (write_offset == -1) {
                    JNU_ThrowIOExceptionWithLastError(env, "Seek on destination failed");
                    return IOS_THROWN;
                }
                written_bytes = pwrite(dstFD, write_ptr, read_bytes - total_written, write_offset);

                if (written_bytes > 0) {
                    total_written += written_bytes;
                    write_ptr += written_bytes;
                } else if (written_bytes == -1) {
                    if (errno == EINTR)
                        continue;
                    JNU_ThrowIOExceptionWithLastError(env, "Write failed");
                    return IOS_THROWN;
                } else {
                    break;
                }
            }
            transferred += total_written;
        } else if (read_bytes == -1) {
            if (errno == EAGAIN)
                return IOS_UNAVAILABLE;
            if (errno == EINTR)
                return IOS_INTERRUPTED;
            JNU_ThrowIOExceptionWithLastError(env, "Read failed");
            return IOS_THROWN;
        } else {
            break;
        }
    }

    return transferred;
}