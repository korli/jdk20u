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