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

#include "client.h"
#include "channels/audio-input/audio-buffer.h"
#include "channels/cliprdr.h"
#include "channels/disp.h"
#include "config.h"
#include "fs.h"
#include "log.h"
#include "rdp.h"
#include "settings.h"
#include "user.h"
#include "glyph.h"
#include "pointer.h"
#include "bitmap.h"
#include "gdi.h"
#include <freerdp/server/proxy.h>

#ifdef ENABLE_COMMON_SSH

#include "common-ssh/sftp.h"
#include "common-ssh/ssh.h"
#include "common-ssh/user.h"

#endif

#include <guacamole/audio.h>
#include <guacamole/client.h>
#include <guacamole/recording.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

/**
 * Tests whether the given path refers to a directory which the current user
 * can write to. If the given path is not a directory, is not writable, or is
 * not a link pointing to a writable directory, this test will fail, and
 * errno will be set appropriately.
 *
 * @param path
 *     The path to test.
 *
 * @return
 *     Non-zero if the given path is (or points to) a writable directory, zero
 *     otherwise.
 */
static int is_writable_directory(const char *path) {

    /* Verify path is writable */
    if (faccessat(AT_FDCWD, path, W_OK, 0))
        return 0;

    /* If writable, verify path is actually a directory */
    DIR *dir = opendir(path);
    if (!dir)
        return 0;

    /* Path is both writable and a directory */
    closedir(dir);
    return 1;

}

int guac_client_init(guac_client *client, int argc, char **argv) {

    /* Automatically set HOME environment variable if unset (FreeRDP's
     * initialization process will fail within freerdp_settings_new() if this
     * is unset) */
    const char *current_home = getenv("HOME");
    if (current_home == NULL) {

        /* Warn if the correct home directory cannot be determined */
        struct passwd *passwd = getpwuid(getuid());
        if (passwd == NULL)
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: The \"HOME\" environment variable is unset and "
                                                      "its correct value could not be automatically determined: "
                                                      "%s", strerror(errno));

            /* Warn if the correct home directory could be determined but can't be
             * assigned */
        else if (setenv("HOME", passwd->pw_dir, 1))
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: The \"HOME\" environment variable is unset "
                                                      "and its correct value (detected as \"%s\") could not be "
                                                      "assigned: %s", passwd->pw_dir, strerror(errno));

            /* HOME has been successfully set */
        else {
            guac_client_log(client, GUAC_LOG_DEBUG, "\"HOME\" "
                                                    "environment variable was unset and has been "
                                                    "automatically set to \"%s\"", passwd->pw_dir);
            current_home = passwd->pw_dir;
        }

    }

    /* Verify that detected home directory is actually writable and actually a
     * directory, as FreeRDP initialization will mysteriously fail otherwise */
    if (current_home != NULL && !is_writable_directory(current_home)) {
        if (errno == EACCES)
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: The current user's home directory (\"%s\") is "
                                                      "not writable, but FreeRDP generally requires a writable "
                                                      "home directory for storage of configuration files and "
                                                      "certificates.", current_home);
        else if (errno == ENOTDIR)
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: The current user's home directory (\"%s\") is "
                                                      "not actually a directory, but FreeRDP generally requires "
                                                      "a writable home directory for storage of configuration "
                                                      "files and certificates.", current_home);
        else
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: Writability of the current user's home "
                                                      "directory (\"%s\") could not be determined: %s",
                            current_home, strerror(errno));
    }

    /* Set client args */
    client->args = GUAC_RDP_CLIENT_ARGS;

    /* Alloc client data */
    guac_rdp_client *rdp_client = calloc(1, sizeof(guac_rdp_client));
    client->data = rdp_client;

    /* Init clipboard */
    rdp_client->clipboard = guac_rdp_clipboard_alloc(client);

    /* Init display update module */
    rdp_client->disp = guac_rdp_disp_alloc(client)TT;

    /* Init multi-touch support module (RDPEI) */
    rdp_client->rdpei = guac_rdp_rdpei_alloc(client);

    /* Redirect FreeRDP log messages to guac_client_log() */
    guac_rdp_redirect_wlog(client);

    /* Recursive attribute for locks */
    pthread_mutexattr_init(&(rdp_client->attributes));
    pthread_mutexattr_settype(&(rdp_client->attributes),
                              PTHREAD_MUTEX_RECURSIVE);

    /* Init required locks */
    pthread_rwlock_init(&(rdp_client->lock), NULL);
    pthread_mutex_init(&(rdp_client->message_lock), &(rdp_client->attributes));

    /* Set handlers */
    client->join_handler = guac_rdp_user_join_handler;
    client->free_handler = guac_rdp_client_free_handler;
    client->leave_handler = guac_rdp_user_leave_handler;

#ifdef ENABLE_COMMON_SSH
    guac_common_ssh_init(client);
#endif

    return 0;

}

int guac_rdp_client_free_handler(guac_client *client) {
    guac_client_log(client, GUAC_LOG_DEBUG, "Freeing client's %p RDP client: %p", client, client->data);

    guac_rdp_client *rdp_client = (guac_rdp_client *) client->data;

    /* Wait for client thread */
    if (rdp_client->client_thread) {
        pthread_join(rdp_client->client_thread, NULL);
    }

    /* Free parsed settings */
    if (rdp_client->settings != NULL) {
        guac_rdp_settings_free(rdp_client->settings);
    }

    /* Clean up clipboard */
    if (rdp_client->clipboard != NULL)
        guac_rdp_clipboard_free(rdp_client->clipboard);

    /* Free display update module */
    if (rdp_client->disp != NULL)
        guac_rdp_disp_free(rdp_client->disp);

    /* Free multi-touch support module (RDPEI) */
    if (rdp_client->rdpei != NULL)
        guac_rdp_rdpei_free(rdp_client->rdpei);

    /* Clean up filesystem, if allocated */
    if (rdp_client->filesystem != NULL)
        guac_rdp_fs_free(rdp_client->filesystem);

    /* End active print job, if any */
    guac_rdp_print_job *job = (guac_rdp_print_job *) rdp_client->active_job;
    if (job != NULL) {
        guac_rdp_print_job_kill(job);
        guac_rdp_print_job_free(job);
        rdp_client->active_job = NULL;
    }

#ifdef ENABLE_COMMON_SSH
    /* Free SFTP filesystem, if loaded */
    if (rdp_client->sftp_filesystem)
        guac_common_ssh_destroy_sftp_filesystem(rdp_client->sftp_filesystem);

    /* Free SFTP session */
    if (rdp_client->sftp_session)
        guac_common_ssh_destroy_session(rdp_client->sftp_session);

    /* Free SFTP user */
    if (rdp_client->sftp_user)
        guac_common_ssh_destroy_user(rdp_client->sftp_user);

    guac_common_ssh_uninit();
#endif

    /* Clean up recording, if in progress */
    if (rdp_client->recording != NULL)
        guac_recording_free(rdp_client->recording);

    /* Clean up audio stream, if allocated */
    if (rdp_client->audio != NULL)
        guac_audio_stream_free(rdp_client->audio);

    /* Clean up audio input buffer, if allocated */
    if (rdp_client->audio_input != NULL)
        guac_rdp_audio_buffer_free(rdp_client->audio_input);

    pthread_rwlock_destroy(&(rdp_client->lock));
    pthread_mutex_destroy(&(rdp_client->message_lock));

    /* Free client data */
    free(rdp_client);

    return 0;

}

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

static void string_hexdump(const BYTE* data, size_t length)
{
    const BYTE* p = data;
    size_t i, line, offset = 0;

    while (offset < length)
    {
        printf("%04" PRIxz " ", offset);

        line = length - offset;

        if (line > 16)
            line = 16;

        for (i = 0; i < line; i++)
            printf("%02" PRIx8 " ", p[i]);

        for (; i < 16; i++)
            printf("   ");

        for (i = 0; i < line; i++)
            printf("%c", (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '.');

        printf("\n");

        offset += line;
        p += line;
    }
}

static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static char* crypto_base64_encode(const BYTE* data, int in_length, size_t* out_length, BOOL with_padding)
{
    int c;
    const BYTE* q;
    char* p;
    char* ret;
    int i = 0;
    int blocks;

    q = data;
    p = ret = (char*)malloc((in_length + 2) / 3 * 4 + 1);
    if (!p)
        return NULL;

    /* b1, b2, b3 are input bytes
     *
     * 0         1         2
     * 012345678901234567890123
     * |  b1  |  b2   |  b3   |
     *
     * [ c1 ]     [  c3 ]
     *      [  c2 ]     [  c4 ]
     *
     * c1, c2, c3, c4 are output chars in base64
     */

    /* first treat complete blocks */
    blocks = in_length - (in_length % 3);
    for (i = 0; i < blocks; i += 3, q += 3)
    {
        c = (q[0] << 16) + (q[1] << 8) + q[2];

        *p++ = base64[(c & 0x00FC0000) >> 18];
        *p++ = base64[(c & 0x0003F000) >> 12];
        *p++ = base64[(c & 0x00000FC0) >> 6];
        *p++ = base64[c & 0x0000003F];
    }

    /* then remainder */
    switch (in_length % 3)
    {
        case 0:
            break;
        case 1:
            c = (q[0] << 16);
            *p++ = base64[(c & 0x00FC0000) >> 18];
            *p++ = base64[(c & 0x0003F000) >> 12];
            if (with_padding) {
                *p++ = '=';
                *p++ = '=';
            }
        break;
        case 2:
            c = (q[0] << 16) + (q[1] << 8);
            *p++ = base64[(c & 0x00FC0000) >> 18];
            *p++ = base64[(c & 0x0003F000) >> 12];
            *p++ = base64[(c & 0x00000FC0) >> 6];
            if (with_padding) {
                *p++ = '=';
            }
        break;
    }

    *p = 0;
    *out_length = (size_t)(p - ret);

    return ret;
}

BOOL array_cmp_str(const void* objA, const void* objB) {
    return strcmp(objA, objB) == 0;
}

long read_n_bytes(int sockfd, void* buffer, size_t n)
{
    ssize_t bytes_read = 0;
    char* buf_ptr = buffer;

    while (bytes_read < n) {
        ssize_t ret = recv(sockfd, buf_ptr + bytes_read, n - bytes_read, 0);

        // Check for errors or connection closure
        if (ret == 0) {
            // The peer closed the connection (EOF)
            return 0;
        } else if (ret < 0) {
            // If interrupted by a signal, just retry
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            // Otherwise, it's a real error
            return -1;
        }

        bytes_read += ret;
    }

    // On success, we have read exactly n bytes
    return bytes_read;
}

static char* read_null_terminated_str(guac_client* client, wStream* s) {
    BYTE* str = NULL;
    size_t len;
    size_t remain = Stream_GetRemainingLength(s);
    str = Stream_Pointer(s);

    if ((len = strnlen((char*)str, remain)) == remain)
    {
        guac_client_log(client, GUAC_LOG_ERROR, "Invalid init packet, no NULL byte found");
        return NULL;
    }
    char* ret = strndup((char*)str, len);

    Stream_Seek(s, len+1);
    return ret;
}

static int read_guac_init(guac_client* client, int sockfd, proxyServer* proxy_srv)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL) failed");
        return -1;
    }

    guac_client_log(client, GUAC_LOG_INFO, "Socket flags: 0x%08x", flags);

    int new_flags = flags;

    // Clear the O_NONBLOCK bit to make the socket blocking
    new_flags &= ~O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, new_flags) < 0) {
        perror("fcntl(F_SETFL) failed");
        return -1;
    }

    guac_client_log(client, GUAC_LOG_INFO, "Set new flags", new_flags);

    BYTE b[4096];
    int read = 0;
    int n = read_n_bytes(sockfd, b, 2);
    if (n <= 0)
        return n;
    if (n != 2)
        return -1;

    uint16_t totalLen = (((uint16_t)(*b)) << 8) + (uint16_t)(*(b + 1));
    read += sizeof(uint16_t);

    guac_client_log(client, GUAC_LOG_INFO, "Init pkt size: %d, reading another %d", totalLen, totalLen-2);

    n = read_n_bytes(sockfd, b + read, totalLen - read);
    if (n <= 0)
        return n;
    if (n != totalLen - read)
        return -1;

    guac_client_log(client, GUAC_LOG_INFO, "SUCCESS!!!");
    string_hexdump(b, totalLen);

    if (fcntl(sockfd, F_SETFL, flags) < 0) {
        perror("fcntl(F_SETFL) failed");
        return -1;
    }

    wStream* s = Stream_New(b+2, totalLen-2);

    int ret = -1;

    char* auth_filename = NULL;
    char* conn_name = NULL, *recording_path = NULL, *recording_name = NULL;
    wArrayList* allowed_principals = ArrayList_New(FALSE);
    allowed_principals->object.fnObjectEquals = array_cmp_str;

    // NEW: Read auth filename (first field) - points to file in /var/lib/guacamole/share/
    auth_filename = read_null_terminated_str(client, s);
    if (auth_filename == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR, "Failed to read auth filename from init packet");
        goto cleanup;
    }
    guac_client_log(client, GUAC_LOG_INFO, "auth_filename: %s", auth_filename);

    if (Stream_GetRemainingLength(s) == 0) {
        guac_client_log(client, GUAC_LOG_ERROR, "Only auth filename in init packet");
        goto cleanup;
    }

    // NEW: Load auth data from file
    char auth_filepath[PATH_MAX];
    snprintf(auth_filepath, sizeof(auth_filepath), "/var/lib/guacamole/share/%s", auth_filename);

    FILE* auth_file = fopen(auth_filepath, "r");
    if (auth_file == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR, "Failed to open auth file: %s", auth_filepath);
        goto cleanup;
    }

    // Read procyonConn (first line) - connection name with <principal> placeholder
    char procyonConn_line[512];
    if (fgets(procyonConn_line, sizeof(procyonConn_line), auth_file) == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR, "Failed to read procyonConn from auth file");
        fclose(auth_file);
        goto cleanup;
    }
    procyonConn_line[strcspn(procyonConn_line, "\n")] = 0;
    conn_name = strdup(procyonConn_line);
    guac_client_log(client, GUAC_LOG_INFO, "procyonConn from auth file: %s", conn_name);

    // Read recPath (second line) - recording path
    char recPath_line[PATH_MAX];
    if (fgets(recPath_line, sizeof(recPath_line), auth_file) == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR, "Failed to read recPath from auth file");
        fclose(auth_file);
        goto cleanup;
    }
    recPath_line[strcspn(recPath_line, "\n")] = 0;
    recording_path = strdup(recPath_line);
    guac_client_log(client, GUAC_LOG_INFO, "recPath from auth file: %s", recording_path);

    // Read recName (third line) - recording name
    char recName_line[PATH_MAX];
    if (fgets(recName_line, sizeof(recName_line), auth_file) == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR, "Failed to read recName from auth file");
        fclose(auth_file);
        goto cleanup;
    }
    recName_line[strcspn(recName_line, "\n")] = 0;
    recording_name = strdup(recName_line);
    guac_client_log(client, GUAC_LOG_INFO, "recName from auth file: %s", recording_name);

    // Read allowed principals (remaining lines)
    char principal_line[256];
    while (fgets(principal_line, sizeof(principal_line), auth_file) != NULL) {
        principal_line[strcspn(principal_line, "\n")] = 0;
        if (strlen(principal_line) > 0) {
            char* principal = strdup(principal_line);
            ArrayList_Add(allowed_principals, principal);
            guac_client_log(client, GUAC_LOG_DEBUG, "allowed principal: %s", principal);
        }
    }
    fclose(auth_file);

    guac_client_log(client, GUAC_LOG_INFO, "Loaded %zu allowed principals from auth file",
                    ArrayList_Count(allowed_principals));

    // All data loaded from auth file - init packet now only contains the auth filename
    // Store allowed principals from file
    proxy_srv->allowed_principals = allowed_principals;
    proxy_srv->session_token = auth_filename; // Store auth filename as session token

    const char* skip_recording = "skip";
    if (strcmp(conn_name, skip_recording) == 0 ||
        strcmp(recording_path, skip_recording) == 0 ||
        strcmp(recording_name, skip_recording) == 0) {

        guac_client_log(client, GUAC_LOG_INFO, "Skipping recording");
        free(conn_name);
        free(recording_path);
        free(recording_name);
        ret = 1;
        goto cleanup;
    }

    guac_rdp_client* rdp_client = client->data;
    if (rdp_client == NULL) {
        ret = -1;
        guac_client_log(client, GUAC_LOG_ERROR, "Failed to get client RDP data");
        goto cleanup;
    }
    // Store auth data loaded from file in proxy_srv
    proxy_srv->session_token = auth_filename; // Store filename for reference
    proxy_srv->conn_name = conn_name;
    rdp_client->settings->recording_path = recording_path;
    rdp_client->settings->recording_name = recording_name;

cleanup:
    Stream_Free(s, FALSE);
    if (ret <= 0) {
        free(auth_filename);
        free(conn_name);
        free(recording_path);
        free(recording_name);
        // free the strings
        size_t count = ArrayList_Count(allowed_principals);
        for (size_t i = 0; i < count; i++)
        {
            char* str = ArrayList_GetItem(allowed_principals, i);
            free(str);
        }
    }
    return ret;
}

int start_recording(const proxyServer* proxy_srv, const char* principal) {
    char* conn_name_base64 = NULL;

    if (proxy_srv == NULL || proxy_srv->guacamole_client == NULL) {
        fprintf(stderr, "proxy_srv or guacamole_client is null\n");
        return -1;
    }

    guac_client* client = proxy_srv->guacamole_client;
    if (client->data == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR,"rdp_client is null");
        return -1;
    }

    guac_rdp_client* rdp_client = client->data;
    if (rdp_client->settings->recording_path == NULL ||
        rdp_client->settings->recording_name == NULL ||
        proxy_srv->conn_name == NULL ||
        principal == NULL) {

        guac_client_log(client, GUAC_LOG_WARNING, "skipping recording");
        return 0;
    }

    const char* principal_var = "<principal>";
    const char* found = strstr(proxy_srv->conn_name, principal_var);
    if (!found) {
        guac_client_log(client, GUAC_LOG_ERROR,
            "Principal var ('%s') missing in conn_name: %s",
            principal_var, proxy_srv->conn_name);
        return -1;
    }

    const size_t principal_len = strlen(principal);
    const size_t conn_name_keep = found - proxy_srv->conn_name;
    const size_t conn_name_len = conn_name_keep + principal_len;
    if (conn_name_len >= PATH_MAX) {
        guac_client_log(client, GUAC_LOG_ERROR,
        "Connection name too long (len: %zu, max: %d): '%s' + '%s'",
            conn_name_len, PATH_MAX, proxy_srv->conn_name, principal);
        return -1;
    }

    BYTE conn_name_base64_in[PATH_MAX];
    char* to = (char*)conn_name_base64_in;
    to = stpncpy(to, proxy_srv->conn_name, conn_name_keep);
    to = stpcpy(to, principal);

    if (conn_name_len != (to - (char*)conn_name_base64_in)) {
        guac_client_log(client, GUAC_LOG_ERROR,
            "Connection name length mismatch (len: %zu, expected: %zu): '%s' + '%s'",
            to - (char*)conn_name_base64_in, conn_name_len, proxy_srv->conn_name, principal);
        return -1;
    }

    size_t conn_name_base64_len;
    conn_name_base64 = crypto_base64_encode(conn_name_base64_in, conn_name_len, &conn_name_base64_len,
        FALSE);
    if (conn_name_base64 == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR, "Failed to base64 encode connection name (len %zu): %s",
            conn_name_len, conn_name_base64_in);
        return -1;
    }
    guac_client_log(client, GUAC_LOG_DEBUG, "conn_name_base64: %s", conn_name_base64);

    const char* conn_name_var = "<connection>";
    found = strstr(rdp_client->settings->recording_path, conn_name_var);
    if (!found) {
        free(conn_name_base64);
        return -1;
    }
    const size_t recording_path_keep = found - rdp_client->settings->recording_path;
    const size_t recording_path_len = recording_path_keep + conn_name_base64_len;

    char* recording_path = malloc(recording_path_len + 1);
    to = recording_path;
    to = stpncpy(to, rdp_client->settings->recording_path, recording_path_keep);
    to = stpcpy(to, conn_name_base64);

    if (recording_path_len != (to - recording_path)) {
        guac_client_log(client, GUAC_LOG_ERROR,
            "Connection name length mismatch (len: %zu, expected: %zu): '%s' + '%s'",
            to - recording_path, recording_path_len, rdp_client->settings->recording_path, conn_name_base64);
        free(conn_name_base64);
        free(recording_path);
        return -1;
    }
    guac_client_log(client, GUAC_LOG_DEBUG, "recording_path: %s", recording_path);

    free(conn_name_base64);
    free(rdp_client->settings->recording_path);
    rdp_client->settings->recording_path = recording_path;

    rdp_client->recording = guac_recording_create(client,
                                              rdp_client->settings->recording_path,
                                              rdp_client->settings->recording_name,
                                              TRUE,
                                              TRUE,
                                              TRUE,
                                              FALSE,
                                              FALSE);
    guac_client_log(client, GUAC_LOG_INFO, "Recording created");

    /* Init random number generator */
    srandom(time(NULL));

    /* Create display */
    rdp_client->display = guac_common_display_alloc(client,
                                                    1600,
                                                    900);

    /* Use lossless compression only if requested (otherwise, use default
     * heuristics) */
    guac_common_display_set_lossless(rdp_client->display, 0);

    rdp_client->current_surface = rdp_client->display->default_surface;

    rdp_client->available_svc = guac_common_list_alloc();

    /* Load keymap into client */
    rdp_client->keyboard = guac_rdp_keyboard_alloc(client, guac_rdp_keymap_find("en-us-qwerty"));

    /* Set default pointer */
    guac_common_cursor_set_pointer(rdp_client->display->cursor);

    /* Signal that reconnect has been completed */
    guac_rdp_disp_reconnect_complete(rdp_client->disp);

    return 1;
}

int guac_rdp_proxy_connect(guac_client *client, int fd) {

    rdpPointer *pointer = NULL;
    rdpGlyph *glyph = NULL;
    rdpBitmap *bitmap = NULL;
    rdpPrimaryUpdate *primary = NULL;
    rdpUpdate *additional_update = NULL;
    proxyServer *proxy_srv = NULL;

    guac_client_log(client, GUAC_LOG_INFO, "Client connected to proxy");

    /* Automatically set HOME environment variable if unset (FreeRDP's
     * initialization process will fail within freerdp_settings_new() if this
     * is unset) */
    const char *current_home = getenv("HOME");
    if (current_home == NULL) {

        /* Warn if the correct home directory cannot be determined */
        struct passwd *passwd = getpwuid(getuid());
        if (passwd == NULL)
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: The \"HOME\" environment variable is unset and "
                                                      "its correct value could not be automatically determined: "
                                                      "%s", strerror(errno));

            /* Warn if the correct home directory could be determined but can't be
             * assigned */
        else if (setenv("HOME", passwd->pw_dir, 1))
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: The \"HOME\" environment variable is unset "
                                                      "and its correct value (detected as \"%s\") could not be "
                                                      "assigned: %s", passwd->pw_dir, strerror(errno));

            /* HOME has been successfully set */
        else {
            guac_client_log(client, GUAC_LOG_DEBUG, "\"HOME\" "
                                                    "environment variable was unset and has been "
                                                    "automatically set to \"%s\"", passwd->pw_dir);
            current_home = passwd->pw_dir;
        }
    }

    /* Verify that detected home directory is actually writable and actually a
     * directory, as FreeRDP initialization will mysteriously fail otherwise */
    if (current_home != NULL && !is_writable_directory(current_home)) {
        if (errno == EACCES)
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: The current user's home directory (\"%s\") is "
                                                      "not writable, but FreeRDP generally requires a writable "
                                                      "home directory for storage of configuration files and "
                                                      "certificates.", current_home);
        else if (errno == ENOTDIR)
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: The current user's home directory (\"%s\") is "
                                                      "not actually a directory, but FreeRDP generally requires "
                                                      "a writable home directory for storage of configuration "
                                                      "files and certificates.", current_home);
        else
            guac_client_log(client, GUAC_LOG_WARNING, "FreeRDP initialization "
                                                      "may fail: Writability of the current user's home "
                                                      "directory (\"%s\") could not be determined: %s",
                            current_home, strerror(errno));
    }

    /* Alloc client data */
    guac_rdp_client *rdp_client = calloc(1, sizeof(guac_rdp_client));
    client->data = rdp_client;

    rdp_client->settings = calloc(1, sizeof(guac_rdp_settings));

    /* Init display update module */
    rdp_client->disp = guac_rdp_disp_alloc(client);

    /* Redirect FreeRDP log messages to guac_client_log() */
    guac_rdp_redirect_wlog(client);

    /* Recursive attribute for locks */
    pthread_mutexattr_init(&(rdp_client->attributes));
    pthread_mutexattr_settype(&(rdp_client->attributes),
                              PTHREAD_MUTEX_RECURSIVE);

    /* Init required locks */
    pthread_rwlock_init(&(rdp_client->lock), NULL);
    pthread_mutex_init(&(rdp_client->message_lock), &(rdp_client->attributes));

    /* Set handlers */
    client->free_handler = guac_rdp_client_free_handler;

    int ret = -1;
    proxyConfig *cfg = pf_server_config_load("/opt/guacamole/config.ini");
    if (!cfg) {
        guac_client_log(client, GUAC_LOG_ERROR, "Proxy config load failed from /opt/guacamole/config.ini");
        goto cleanup;
    }
    guac_client_log(client, GUAC_LOG_INFO, "Proxy config loaded");
    pf_server_config_print(cfg);

    additional_update = (rdpUpdate *) calloc(1, sizeof(rdpUpdate));
    additional_update->primary = (rdpPrimaryUpdate *) calloc(1, sizeof(rdpPrimaryUpdate));
    additional_update->DesktopResize = guac_rdp_gdi_desktop_resize;
    additional_update->EndPaint = guac_rdp_gdi_end_paint;
    additional_update->SetBounds = guac_rdp_gdi_set_bounds;

    primary = additional_update->primary;
    primary->DstBlt = guac_rdp_gdi_dstblt;
    primary->PatBlt = guac_rdp_gdi_patblt;
    primary->ScrBlt = guac_rdp_gdi_scrblt;
    primary->MemBlt = guac_rdp_gdi_memblt;
    primary->OpaqueRect = guac_rdp_gdi_opaquerect;

    bitmap = calloc(1, sizeof(rdpBitmap));
    bitmap->New = guac_rdp_bitmap_new;
    bitmap->Free = guac_rdp_bitmap_free;
    bitmap->Paint = guac_rdp_bitmap_paint;
    bitmap->SetSurface = guac_rdp_bitmap_setsurface;

    /* Set up glyph handling */
    glyph = calloc(1, sizeof(rdpGlyph));
    glyph->New = guac_rdp_glyph_new;
    glyph->Free = guac_rdp_glyph_free;
    glyph->Draw = guac_rdp_glyph_draw;
    glyph->BeginDraw = guac_rdp_glyph_begindraw;
    glyph->EndDraw = guac_rdp_glyph_enddraw;

    /* Set up pointer handling */
    pointer = calloc(1, sizeof(rdpPointer));
    pointer->New = guac_rdp_pointer_new;
    pointer->Free = guac_rdp_pointer_free;
    pointer->Set = guac_rdp_pointer_set;
    pointer->SetNull = guac_rdp_pointer_set_null;
    pointer->SetDefault = guac_rdp_pointer_set_default;
    pointer->SetPosition = guac_rdp_pointer_set_position;

    proxy_srv = pf_server_new(cfg);
    if (!proxy_srv) {
        guac_client_log(client, GUAC_LOG_ERROR, "pf_server_new failed");
        goto cleanup;
    }
    guac_client_log(client, GUAC_LOG_INFO, "Proxy created");

    int received = (client, fd, proxy_srv);
    if (received == 0) {
        guac_client_log(client, GUAC_LOG_ERROR, "Failed to read init, connection closed");
        goto cleanup;
    }
    if (received < 0) {
        guac_client_log(client, GUAC_LOG_ERROR, "Failed to read init, ERRORED");
        goto cleanup;
    }

    proxy_srv->guacamole_client = client;
    proxy_srv->pointer = pointer;
    proxy_srv->glyph = glyph;
    proxy_srv->bitmap = bitmap;
    proxy_srv->additional_update = additional_update;
    proxy_srv->is_native = TRUE;
    proxy_srv->start_recording = start_recording;

    ret = pf_server_start_with_peer_socket(proxy_srv, fd);
    if (!ret) {
        ret = -1;
        guac_client_log(client, GUAC_LOG_ERROR, "pf_server_start_with_peer_socket failed");
        goto cleanup;
    }
    pf_server_print_plugins_info();

    guac_client_log(client, GUAC_LOG_INFO, "Proxy server started, waiting for start event");

    const HANDLE evs[] = {proxy_srv->start_recording_event, proxy_srv->thread};
    DWORD wait_ret = WaitForMultipleObjects(2, evs, FALSE, 30000);

    if (wait_ret == WAIT_OBJECT_0) {
        guac_client_log(client, GUAC_LOG_INFO, "Proxy server started");
    } else if (wait_ret == WAIT_OBJECT_0 + 1) {
        guac_client_log(client, GUAC_LOG_ERROR, "Proxy connection finished");
        goto cleanup;
    } else if (wait_ret == WAIT_TIMEOUT) {
        guac_client_log(client, GUAC_LOG_ERROR, "WaitForMultipleObjects timed out for server start event");
        pf_server_stop(proxy_srv);
        goto cleanup;
    } else {
        guac_client_log(client, GUAC_LOG_ERROR,
            "WaitForMultipleObjects failed for server start event: 0x%08X", wait_ret);
        pf_server_stop(proxy_srv);
        goto cleanup;
    }

    if (rdp_client->recording == NULL) {
        guac_client_log(client, GUAC_LOG_WARNING, "No recording started - wait for connection to finish");

        WaitForSingleObject(proxy_srv->thread, INFINITE);
        guac_client_log(client, GUAC_LOG_INFO, "Connection finished");
        ret = 0;
        goto cleanup;
    }

    while (1) {
        wait_ret = WaitForSingleObject(proxy_srv->thread, GUAC_RDP_FRAME_DURATION);
        if (wait_ret == WAIT_OBJECT_0) {
            ret = 0;
            guac_client_log(client, GUAC_LOG_INFO, "Proxy thread stop event");
            break;
        }
        if (wait_ret == WAIT_TIMEOUT) {
            guac_recording_report_mouse(rdp_client->recording, rdp_client->display->cursor->x,
                rdp_client->display->cursor->y, 0);
            guac_common_display_flush(rdp_client->display);
            guac_client_end_frame(client);
            guac_socket_flush(client->socket);
        }
        else {
            ret = -1;
            guac_client_log(client, GUAC_LOG_ERROR, "WaitForSingleObject failed for server stop event with: %d",
                            wait_ret);
            break;
        }
    }
    pf_server_stop(proxy_srv);

cleanup:
    /* Clean up print job, if active */
    if (rdp_client->active_job != NULL) {
        guac_rdp_print_job_kill(rdp_client->active_job);
        guac_rdp_print_job_free(rdp_client->active_job);
    }

    /* Free SVC list */
    guac_common_list_free(rdp_client->available_svc);
    rdp_client->available_svc = NULL;

    /* Free RDP keyboard state */
    if (rdp_client->keyboard) {
        guac_rdp_keyboard_free(rdp_client->keyboard);
        rdp_client->keyboard = NULL;
    }

    /* Free display */
    if (rdp_client->display) {
        guac_common_display_free(rdp_client->display);
        rdp_client->display = NULL;
    }

    close(fd);
    guac_client_free(client);
    if (additional_update) {
        free(additional_update->primary);
        free(additional_update);
    }
    free(pointer);
    free(glyph);
    free(bitmap);
    if (proxy_srv) {
        if (proxy_srv->allowed_principals) {
            for (int i = 0; i < ArrayList_Count(proxy_srv->allowed_principals); i++) {
                free(ArrayList_GetItem(proxy_srv->allowed_principals, i));
            }
            free(proxy_srv->allowed_principals);
        }
        free((void*)proxy_srv->conn_name);
        pf_server_free(proxy_srv);
    }

    return ret;
}
