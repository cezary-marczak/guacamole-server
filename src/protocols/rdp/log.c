/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <errno.h>
#include <guacamole/client.h>
#include <winpr/wlog.h>
#include <winpr/wtypes.h>

#include <bits/syscall.h>
#include <unistd.h>

/**
 * The guac_client that should be used within this process for FreeRDP log
 * messages. As all Guacamole connections are isolated at the process level,
 * this will only ever be set to the guac_client of the current process'
 * connection.
 */
static FILE* pkt_log_file = NULL;
static int log_level = GUAC_LOG_INFO;

static void string_hexdump(const BYTE* data, const char* prefix, DWORD flags, FILE* outfile, char* buffer,
                           size_t length)
{
    const BYTE* p = data;
    size_t i, line, offset = 0;
    int written = 0;

    written += sprintf(buffer, "%s flags 0x%04X:\n", prefix, flags);

    while (offset < length)
    {
        if (4096 - written < 60)
        {
            fwrite(buffer, 1, written, outfile);
            written = 0;
        }
        written += sprintf(buffer + written, "%04" PRIxz " ", offset); // 5

        line = length - offset;

        if (line > 16)
            line = 16;

        for (i = 0; i < line; i++)
            written += sprintf(buffer + written, "%02" PRIx8 " ", p[i]); // 3*16

        written += sprintf(buffer + written, "\n"); // 1

        offset += line;
        p += line;
    }
    fwrite(buffer, 1, written, outfile);
}

static BOOL guac_rdp_wlog_packet(const wLogMessage* message)
{
    if (pkt_log_file == NULL)
    {
        pid_t pid = getpid();
        unsigned long tid = (size_t)syscall(SYS_gettid);
        char filename[FILENAME_MAX];
        int used = snprintf(filename, FILENAME_MAX, "/home/guacd/rdp-packets-%d-%lu.log", pid, tid);
        if (used < 0)
        {
            return FALSE;
        }
        if (used >= FILENAME_MAX)
        {
            filename[FILENAME_MAX - 1] = '\0';
        }
        pkt_log_file = fopen(filename, "w");
        if (pkt_log_file == NULL)
        {
            printf("Failed to open: %s with error: %s", filename, strerror(errno));
            return FALSE;
        }
    }
    char buffer[4096];

    string_hexdump(message->PacketData, message->PrefixString, message->PacketFlags, pkt_log_file, buffer,
                   message->PacketLength);
    return TRUE;
}

/**
 * Logs the text data within the given message to the logging facilities of the
 * guac_client currently stored under current_client (the guac_client of the
 * current process).
 *
 * @param message
 *     The message to log.
 *
 * @return
 *     TRUE if the message was successfully logged, FALSE otherwise.
 */
static BOOL guac_rdp_wlog_text_message(const wLogMessage* message) {

    guac_client_log_level gl;
    switch (message->Level)
    {
    case WLOG_TRACE:
        gl = GUAC_LOG_TRACE;
        break;
    case WLOG_DEBUG:
        gl = GUAC_LOG_DEBUG;
        break;
    case WLOG_INFO:
        gl = GUAC_LOG_INFO;
        break;
    case WLOG_WARN:
        gl = GUAC_LOG_WARNING;
        break;
    case WLOG_ERROR:
        gl = GUAC_LOG_ERROR;
        break;
    case WLOG_FATAL:
        gl = GUAC_LOG_ERROR;
        break;
    default:
        gl = GUAC_LOG_INFO;
        break;
    }

    if (gl > log_level)
    {
        return TRUE;
    }
    printf("%s%s\n", message->PrefixString, message->TextString);
    return TRUE;
}

void guac_rdp_redirect_wlog(guac_client* client) {

    wLogCallbacks callbacks = {.message = guac_rdp_wlog_text_message, .package = guac_rdp_wlog_packet};

    log_level = client->log_level;
    /* Reconfigure root logger to use callback appender */
    wLog* root = WLog_GetRoot();
    WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK);

    /* Set appender callbacks to our own */
    wLogAppender* appender = WLog_GetLogAppender(root);
    WLog_ConfigureAppender(appender, "callbacks", &callbacks);

}
