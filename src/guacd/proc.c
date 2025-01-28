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

#include "config.h"

#include "log.h"
#include "move-fd.h"
#include "proc.h"
#include "proc-map.h"

#include <guacamole/client.h>
#include <guacamole/error.h>
#include <guacamole/parser.h>
#include <guacamole/plugin.h>
#include <guacamole/protocol.h>
#include <guacamole/socket.h>
#include <guacamole/user.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

/**
 * Parameters for the user thread.
 */
typedef struct guacd_user_thread_params {

    /**
     * The process being joined.
     */
    guacd_proc* proc;

    /**
     * The file descriptor of the joining user's socket.
     */
    int fd;

    /**
     * Whether the joining user is the connection owner.
     */
    int owner;

} guacd_user_thread_params;

//guac_rdp_settings* guac_rdp_parse_args(guac_user* user,
//                                       int argc, const char** argv) {
//
//    /* Validate arg count */
//    if (argc != RDP_ARGS_COUNT) {
//        guac_user_log(user, GUAC_LOG_WARNING, "Incorrect number of connection "
//                                              "parameters provided: expected %i, got %i.",
//                      RDP_ARGS_COUNT, argc);
//        return NULL;
//    }
//
//    guac_rdp_settings* settings = calloc(1, sizeof(guac_rdp_settings));
//
//    /* Use console */
//    settings->console =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_CONSOLE, 0);
//
//    /* Enable/disable console audio */
//    settings->console_audio =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_CONSOLE_AUDIO, 0);
//
//    /* Ignore SSL/TLS certificate */
//    settings->ignore_certificate =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_IGNORE_CERT, 0);
//
//    /* Disable authentication */
//    settings->disable_authentication =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_DISABLE_AUTH, 0);
//
//    /* NLA security */
//    if (strcmp(argv[IDX_SECURITY], "nla") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Security mode: NLA");
//        settings->security_mode = GUAC_SECURITY_NLA;
//
//        /*
//         * NLA is known not to work with FIPS; allow the mode selection but
//         * warn that it will not work.
//         */
//        if (guac_fips_enabled())
//            guac_user_log(user, GUAC_LOG_WARNING, fips_nla_mode_warning);
//
//    }
//
//        /* Extended NLA security */
//    else if (strcmp(argv[IDX_SECURITY], "nla-ext") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Security mode: Extended NLA");
//        settings->security_mode = GUAC_SECURITY_EXTENDED_NLA;
//
//        /*
//         * NLA is known not to work with FIPS; allow the mode selection but
//         * warn that it will not work.
//         */
//        if (guac_fips_enabled())
//            guac_user_log(user, GUAC_LOG_WARNING, fips_nla_mode_warning);
//    }
//
//        /* TLS security */
//    else if (strcmp(argv[IDX_SECURITY], "tls") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Security mode: TLS");
//        settings->security_mode = GUAC_SECURITY_TLS;
//    }
//
//        /* RDP security */
//    else if (strcmp(argv[IDX_SECURITY], "rdp") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Security mode: RDP");
//        settings->security_mode = GUAC_SECURITY_RDP;
//    }
//
//        /* Negotiate security supported by VMConnect */
//    else if (strcmp(argv[IDX_SECURITY], "vmconnect") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Security mode: Hyper-V / VMConnect");
//        settings->security_mode = GUAC_SECURITY_VMCONNECT;
//    }
//
//        /* Negotiate security (allow server to choose) */
//    else if (strcmp(argv[IDX_SECURITY], "any") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Security mode: Negotiate (ANY)");
//        settings->security_mode = GUAC_SECURITY_ANY;
//    }
//
//        /* If nothing given, default to RDP */
//    else {
//        guac_user_log(user, GUAC_LOG_INFO, "No security mode specified. Defaulting to security mode negotiation with server.");
//        settings->security_mode = GUAC_SECURITY_ANY;
//    }
//
//    /* Set hostname */
//    settings->hostname =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_HOSTNAME, "");
//
//    /* If port specified, use it, otherwise use an appropriate default */
//    settings->port =
//            guac_user_parse_args_int(user, GUAC_RDP_CLIENT_ARGS, argv, IDX_PORT,
//                                     settings->security_mode == GUAC_SECURITY_VMCONNECT ? RDP_DEFAULT_VMCONNECT_PORT : RDP_DEFAULT_PORT);
//
//    guac_user_log(user, GUAC_LOG_DEBUG,
//                  "User resolution is %ix%i at %i DPI",
//                  user->info.optimal_width,
//                  user->info.optimal_height,
//                  user->info.optimal_resolution);
//
//    /* Use suggested resolution unless overridden */
//    settings->resolution =
//            guac_user_parse_args_int(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                     IDX_DPI, guac_rdp_suggest_resolution(user));
//
//    /* Use optimal width unless overridden */
//    settings->width = user->info.optimal_width
//                      * settings->resolution
//                      / user->info.optimal_resolution;
//
//    if (argv[IDX_WIDTH][0] != '\0')
//        settings->width = atoi(argv[IDX_WIDTH]);
//
//    /* Use default width if given width is invalid. */
//    if (settings->width <= 0) {
//        settings->width = RDP_DEFAULT_WIDTH;
//        guac_user_log(user, GUAC_LOG_ERROR,
//                      "Invalid width: \"%s\". Using default of %i.",
//                      argv[IDX_WIDTH], settings->width);
//    }
//
//    /* Round width down to nearest multiple of 4 */
//    settings->width = settings->width & ~0x3;
//
//    /* Use optimal height unless overridden */
//    settings->height = user->info.optimal_height
//                       * settings->resolution
//                       / user->info.optimal_resolution;
//
//    if (argv[IDX_HEIGHT][0] != '\0')
//        settings->height = atoi(argv[IDX_HEIGHT]);
//
//    /* Use default height if given height is invalid. */
//    if (settings->height <= 0) {
//        settings->height = RDP_DEFAULT_HEIGHT;
//        guac_user_log(user, GUAC_LOG_ERROR,
//                      "Invalid height: \"%s\". Using default of %i.",
//                      argv[IDX_WIDTH], settings->height);
//    }
//
//    guac_user_log(user, GUAC_LOG_DEBUG,
//                  "Using resolution of %ix%i at %i DPI",
//                  settings->width,
//                  settings->height,
//                  settings->resolution);
//
//    /* Lossless compression */
//    settings->lossless =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_FORCE_LOSSLESS, 0);
//
//    /* Domain */
//    settings->domain =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_DOMAIN, NULL);
//
//    /* Username */
//    settings->username =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_USERNAME, NULL);
//
//    /* Password */
//    settings->password =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_PASSWORD, NULL);
//
//    /* Read-only mode */
//    settings->read_only =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_READ_ONLY, 0);
//
//    /* Client name */
//    settings->client_name =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_CLIENT_NAME, "Guacamole RDP");
//
//    /* Initial program */
//    settings->initial_program =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_INITIAL_PROGRAM, NULL);
//
//    /* RemoteApp program */
//    settings->remote_app =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_REMOTE_APP, NULL);
//
//    /* RemoteApp working directory */
//    settings->remote_app_dir =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_REMOTE_APP_DIR, NULL);
//
//    /* RemoteApp arguments */
//    settings->remote_app_args =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_REMOTE_APP_ARGS, NULL);
//
//    /* Static virtual channels */
//    settings->svc_names = NULL;
//    if (argv[IDX_STATIC_CHANNELS][0] != '\0')
//        settings->svc_names = guac_split(argv[IDX_STATIC_CHANNELS], ',');
//
//    /*
//     * Performance flags
//     */
//
//    settings->wallpaper_enabled =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_WALLPAPER, 0);
//
//    settings->theming_enabled =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_THEMING, 0);
//
//    settings->font_smoothing_enabled =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_FONT_SMOOTHING, 0);
//
//    settings->full_window_drag_enabled =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_FULL_WINDOW_DRAG, 0);
//
//    settings->desktop_composition_enabled =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_DESKTOP_COMPOSITION, 0);
//
//    settings->menu_animations_enabled =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_MENU_ANIMATIONS, 0);
//
//    settings->disable_bitmap_caching =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_DISABLE_BITMAP_CACHING, 0);
//
//    settings->disable_offscreen_caching =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_DISABLE_OFFSCREEN_CACHING, 0);
//
//    /* FreeRDP does not consider the glyph cache implementation to be stable as
//     * of 2.0.0, and it MUST NOT be used. Usage of the glyph cache results in
//     * unexpected disconnects when using older versions of Windows and recent
//     * versions of FreeRDP. See: https://issues.apache.org/jira/browse/GUACAMOLE-1191 */
//    settings->disable_glyph_caching = 1;
//
//    /* In case the user expects glyph caching to be enabled, either explicitly
//     * or by default, warn that this will not be the case as the glyph cache
//     * is not considered stable. */
//    if (!guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                      IDX_DISABLE_GLYPH_CACHING, 0)) {
//        guac_user_log(user, GUAC_LOG_DEBUG, "Glyph caching is currently "
//                                            "universally disabled, regardless of the value of the \"%s\" "
//                                            "parameter, as glyph caching support is not considered stable "
//                                            "by FreeRDP as of the FreeRDP 2.0.0 release. See: "
//                                            "https://issues.apache.org/jira/browse/GUACAMOLE-1191",
//                      GUAC_RDP_CLIENT_ARGS[IDX_DISABLE_GLYPH_CACHING]);
//    }
//
//    /* Session color depth */
//    settings->color_depth =
//            guac_user_parse_args_int(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                     IDX_COLOR_DEPTH, RDP_DEFAULT_DEPTH);
//
//    /* Preconnection ID */
//    settings->preconnection_id = -1;
//    if (argv[IDX_PRECONNECTION_ID][0] != '\0') {
//
//        /* Parse preconnection ID, warn if invalid */
//        int preconnection_id = atoi(argv[IDX_PRECONNECTION_ID]);
//        if (preconnection_id < 0)
//            guac_user_log(user, GUAC_LOG_WARNING,
//                          "Ignoring invalid preconnection ID: %i",
//                          preconnection_id);
//
//            /* Otherwise, assign specified ID */
//        else {
//            settings->preconnection_id = preconnection_id;
//            guac_user_log(user, GUAC_LOG_DEBUG,
//                          "Preconnection ID: %i", settings->preconnection_id);
//        }
//
//    }
//
//    /* Preconnection BLOB */
//    settings->preconnection_blob = NULL;
//    if (argv[IDX_PRECONNECTION_BLOB][0] != '\0') {
//        settings->preconnection_blob = strdup(argv[IDX_PRECONNECTION_BLOB]);
//        guac_user_log(user, GUAC_LOG_DEBUG,
//                      "Preconnection BLOB: \"%s\"", settings->preconnection_blob);
//    }
//
//    /* Audio enable/disable */
//    settings->audio_enabled =
//            !guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                          IDX_DISABLE_AUDIO, 0);
//
//    /* Printing enable/disable */
//    settings->printing_enabled =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_PRINTING, 0);
//
//    /* Name of redirected printer */
//    settings->printer_name =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_PRINTER_NAME, "Guacamole Printer");
//
//    /* Drive enable/disable */
//    settings->drive_enabled =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_DRIVE, 0);
//
//    /* Name of the drive being passed through */
//    settings->drive_name =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_DRIVE_NAME, "Guacamole Filesystem");
//
//    /* The path on the server to connect the drive. */
//    settings->drive_path =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_DRIVE_PATH, "");
//
//    /* If the server path should be created if it doesn't already exist. */
//    settings->create_drive_path =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_CREATE_DRIVE_PATH, 0);
//
//    /* If file downloads over RDP should be disabled. */
//    settings->disable_download =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_DISABLE_DOWNLOAD, 0);
//
//    /* If file uploads over RDP should be disabled. */
//    settings->disable_upload =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_DISABLE_UPLOAD, 0);
//
//    /* Pick keymap based on argument */
//    settings->server_layout = NULL;
//    if (argv[IDX_SERVER_LAYOUT][0] != '\0')
//        settings->server_layout =
//                guac_rdp_keymap_find(argv[IDX_SERVER_LAYOUT]);
//
//    /* If no keymap requested, use default */
//    if (settings->server_layout == NULL)
//        settings->server_layout = guac_rdp_keymap_find(GUAC_DEFAULT_KEYMAP);
//
//    /* Timezone if provided by client, or use handshake version */
//    settings->timezone =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_TIMEZONE, user->info.timezone);
//
//#ifdef ENABLE_COMMON_SSH
//    /* SFTP enable/disable */
//    settings->enable_sftp =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_SFTP, 0);
//
//    /* Hostname for SFTP connection */
//    settings->sftp_hostname =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_SFTP_HOSTNAME, settings->hostname);
//
//    /* The public SSH host key. */
//    settings->sftp_host_key =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_SFTP_HOST_KEY, NULL);
//
//    /* Port for SFTP connection */
//    settings->sftp_port =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_SFTP_PORT, "22");
//
//    /* Username for SSH/SFTP authentication */
//    settings->sftp_username =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_SFTP_USERNAME,
//                                        settings->username != NULL ? settings->username : "");
//
//    /* Password for SFTP (if not using private key) */
//    settings->sftp_password =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_SFTP_PASSWORD, "");
//
//    /* Private key for SFTP (if not using password) */
//    settings->sftp_private_key =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_SFTP_PRIVATE_KEY, NULL);
//
//    /* Passphrase for decrypting the SFTP private key (if applicable */
//    settings->sftp_passphrase =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_SFTP_PASSPHRASE, "");
//
//    /* Default upload directory */
//    settings->sftp_directory =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_SFTP_DIRECTORY, NULL);
//
//    /* SFTP root directory */
//    settings->sftp_root_directory =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_SFTP_ROOT_DIRECTORY, "/");
//
//    /* Default keepalive value */
//    settings->sftp_server_alive_interval =
//            guac_user_parse_args_int(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                     IDX_SFTP_SERVER_ALIVE_INTERVAL, 0);
//
//    /* Whether or not to disable file download over SFTP. */
//    settings->sftp_disable_download =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_SFTP_DISABLE_DOWNLOAD, 0);
//
//    /* Whether or not to disable file upload over SFTP. */
//    settings->sftp_disable_upload =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_SFTP_DISABLE_UPLOAD, 0);
//#endif
//
//    /* Read recording path */
//    settings->recording_path =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_RECORDING_PATH, NULL);
//
//    /* Read recording name */
//    settings->recording_name =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_RECORDING_NAME, GUAC_RDP_DEFAULT_RECORDING_NAME);
//
//    /* Parse output exclusion flag */
//    settings->recording_exclude_output =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_RECORDING_EXCLUDE_OUTPUT, 0);
//
//    /* Parse mouse exclusion flag */
//    settings->recording_exclude_mouse =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_RECORDING_EXCLUDE_MOUSE, 0);
//
//    /* Parse touch exclusion flag */
//    settings->recording_exclude_touch =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_RECORDING_EXCLUDE_TOUCH, 0);
//
//    /* Parse key event inclusion flag */
//    settings->recording_include_keys =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_RECORDING_INCLUDE_KEYS, 0);
//
//    /* Parse path creation flag */
//    settings->create_recording_path =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_CREATE_RECORDING_PATH, 0);
//
//    /* No resize method */
//    if (strcmp(argv[IDX_RESIZE_METHOD], "") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Resize method: none");
//        settings->resize_method = GUAC_RESIZE_NONE;
//    }
//
//        /* Resize method: "reconnect" */
//    else if (strcmp(argv[IDX_RESIZE_METHOD], "reconnect") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Resize method: reconnect");
//        settings->resize_method = GUAC_RESIZE_RECONNECT;
//    }
//
//        /* Resize method: "display-update" */
//    else if (strcmp(argv[IDX_RESIZE_METHOD], "display-update") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Resize method: display-update");
//        settings->resize_method = GUAC_RESIZE_DISPLAY_UPDATE;
//    }
//
//        /* Default to no resize method if invalid */
//    else {
//        guac_user_log(user, GUAC_LOG_INFO, "Resize method \"%s\" invalid. ",
//                      "Defaulting to no resize method.", argv[IDX_RESIZE_METHOD]);
//        settings->resize_method = GUAC_RESIZE_NONE;
//    }
//
//    /* Multi-touch input enable/disable */
//    settings->enable_touch =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_TOUCH, 0);
//
//    /* Audio input enable/disable */
//    settings->enable_audio_input =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_ENABLE_AUDIO_INPUT, 0);
//
//    /* Set gateway hostname */
//    settings->gateway_hostname =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_GATEWAY_HOSTNAME, NULL);
//
//    /* If gateway port specified, use it */
//    settings->gateway_port =
//            guac_user_parse_args_int(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                     IDX_GATEWAY_PORT, 443);
//
//    /* Set gateway domain */
//    settings->gateway_domain =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_GATEWAY_DOMAIN, NULL);
//
//    /* Set gateway username */
//    settings->gateway_username =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_GATEWAY_USERNAME, NULL);
//
//    /* Set gateway password */
//    settings->gateway_password =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_GATEWAY_PASSWORD, NULL);
//
//    /* Set load balance info */
//    settings->load_balance_info =
//            guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                        IDX_LOAD_BALANCE_INFO, NULL);
//
//    /* Parse clipboard copy disable flag */
//    settings->disable_copy =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_DISABLE_COPY, 0);
//
//    /* Parse clipboard paste disable flag */
//    settings->disable_paste =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_DISABLE_PASTE, 0);
//
//    /* Normalize clipboard line endings to Unix format */
//    if (strcmp(argv[IDX_NORMALIZE_CLIPBOARD], "unix") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Clipboard line ending normalization: Unix (LF)");
//        settings->normalize_clipboard = 1;
//        settings->clipboard_crlf = 0;
//    }
//
//        /* Normalize clipboard line endings to Windows format */
//    else if (strcmp(argv[IDX_NORMALIZE_CLIPBOARD], "windows") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Clipboard line ending normalization: Windows (CRLF)");
//        settings->normalize_clipboard = 1;
//        settings->clipboard_crlf = 1;
//    }
//
//        /* Preserve clipboard line ending format */
//    else if (strcmp(argv[IDX_NORMALIZE_CLIPBOARD], "preserve") == 0) {
//        guac_user_log(user, GUAC_LOG_INFO, "Clipboard line ending normalization: Preserve (none)");
//        settings->normalize_clipboard = 0;
//        settings->clipboard_crlf = 0;
//    }
//
//        /* If nothing given, default to preserving line endings */
//    else {
//        guac_user_log(user, GUAC_LOG_INFO, "No clipboard line-ending normalization specified. Defaulting to preserving the format of all line endings.");
//        settings->normalize_clipboard = 0;
//        settings->clipboard_crlf = 0;
//    }
//
//
//    /* Parse Wake-on-LAN (WoL) settings */
//    settings->wol_send_packet =
//            guac_user_parse_args_boolean(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_WOL_SEND_PACKET, 0);
//
//    if (settings->wol_send_packet) {
//
//        /* If WoL has been requested but no MAC address given, log a warning. */
//        if(strcmp(argv[IDX_WOL_MAC_ADDR], "") == 0) {
//            guac_user_log(user, GUAC_LOG_WARNING, "WoL requested but no MAC ",
//                          "address specified.  WoL will not be sent.");
//            settings->wol_send_packet = 0;
//        }
//
//        /* Parse the WoL MAC address. */
//        settings->wol_mac_addr =
//                guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                            IDX_WOL_MAC_ADDR, NULL);
//
//        /* Parse the WoL broadcast address. */
//        settings->wol_broadcast_addr =
//                guac_user_parse_args_string(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                            IDX_WOL_BROADCAST_ADDR, GUAC_WOL_LOCAL_IPV4_BROADCAST);
//
//        /* Parse the WoL broadcast port. */
//        settings->wol_udp_port = (unsigned short)
//                guac_user_parse_args_int(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_WOL_UDP_PORT, GUAC_WOL_PORT);
//
//        /* Parse the WoL wait time. */
//        settings->wol_wait_time =
//                guac_user_parse_args_int(user, GUAC_RDP_CLIENT_ARGS, argv,
//                                         IDX_WOL_WAIT_TIME, GUAC_WOL_DEFAULT_BOOT_WAIT_TIME);
//
//    }
//
//    /* Success */
//    return settings;
//
//}


/**
 * Handles a user's entire connection and socket lifecycle.
 *
 * @param data
 *     A pointer to a guacd_user_thread_params structure describing the user's
 *     associated file descriptor, whether that user is the connection owner
 *     (the first person to join), as well as the process associated with the
 *     connection being joined.
 *
 * @return
 *     Always NULL.
 */
static void* guacd_user_thread(void* data) {

    guacd_user_thread_params* params = (guacd_user_thread_params*) data;
    guacd_proc* proc = params->proc;
    guac_client* client = proc->client;

    /* Get guac_socket for user's file descriptor */
    guac_socket* socket = guac_socket_open(params->fd);
    if (socket == NULL)
        return NULL;

    /* Create skeleton user */
    guac_user* user = guac_user_alloc();
    user->socket = socket;
    user->client = client;
    user->owner  = params->owner;

    /* Handle user connection from handshake until disconnect/completion */
    guac_user_handle_connection(user, GUACD_USEC_TIMEOUT);

    /* Stop client and prevent future users if all users are disconnected */
    if (client->connected_users == 0) {
        guacd_log(GUAC_LOG_INFO, "Last user of connection \"%s\" disconnected", client->connection_id);
        guacd_proc_stop(proc);
    }

    /* Clean up */
    guac_socket_free(socket);
    guac_user_free(user);
    free(params);

    return NULL;

}

/**
 * Begins a new user connection under a given process, using the given file
 * descriptor. The connection will be managed by a separate and detached thread
 * which is started by this function.
 *
 * @param proc
 *     The process that the user is being added to.
 *
 * @param fd
 *     The file descriptor associated with the user's network connection to
 *     guacd.
 *
 * @param owner
 *     Non-zero if the user is the owner of the connection being joined (they
 *     are the first user to join), or zero otherwise.
 */
static void guacd_proc_add_user(guacd_proc* proc, int fd, int owner) {

    guacd_user_thread_params* params = malloc(sizeof(guacd_user_thread_params));
    params->proc = proc;
    params->fd = fd;
    params->owner = owner;

    /* Start user thread */
    pthread_t user_thread;
    pthread_create(&user_thread, NULL, guacd_user_thread, params);
    pthread_detach(user_thread);

}

/**
 * Forcibly kills all processes within the current process group, including the
 * current process and all child processes. This function is only safe to call
 * if the process group ID has been correctly set. Calling this function within
 * a process which does not have a PGID separate from the main guacd process
 * can result in guacd itself being terminated.
 */
static void guacd_kill_current_proc_group() {

    /* Forcibly kill all children within process group */
    if (kill(0, SIGKILL))
        guacd_log(GUAC_LOG_WARNING, "Unable to forcibly terminate "
                "client process: %s ", strerror(errno));

}

/**
 * The current status of a background attempt to free a guac_client instance.
 */
typedef struct guacd_client_free {

    /**
     * The guac_client instance being freed.
     */
    guac_client* client;

    /**
     * The condition which is signalled whenever changes are made to the
     * completed flag. The completed flag only changes from zero (not yet
     * freed) to non-zero (successfully freed).
     */
    pthread_cond_t completed_cond;

    /**
     * Mutex which must be acquired before any changes are made to the
     * completed flag.
     */
    pthread_mutex_t completed_mutex;

    /**
     * Whether the guac_client has been successfully freed. Initially, this
     * will be zero, indicating that the free operation has not yet been
     * attempted. If the client is eventually successfully freed, this will be
     * set to a non-zero value. Changes to this flag are signalled through
     * the completed_cond condition.
     */
    int completed;

} guacd_client_free;

/**
 * Thread which frees a given guac_client instance in the background. If the
 * free operation succeeds, a flag is set on the provided structure, and the
 * change in that flag is signalled with a pthread condition.
 *
 * At the time this function is provided to a pthread_create() call, the
 * completed flag of the associated guacd_client_free structure MUST be
 * initialized to zero, the pthread mutex and condition MUST both be
 * initialized, and the client pointer must point to the guac_client being
 * freed.
 *
 * @param data
 *     A pointer to a guacd_client_free structure describing the free
 *     operation.
 *
 * @return
 *     Always NULL.
 */
static void* guacd_client_free_thread(void* data) {

    guacd_client_free* free_operation = (guacd_client_free*) data;

    /* Attempt to free client (this may never return if the client is
     * malfunctioning) */
    guac_client_free(free_operation->client);

    /* Signal that the client was successfully freed */
    pthread_mutex_lock(&free_operation->completed_mutex);
    free_operation->completed = 1;
    pthread_cond_broadcast(&free_operation->completed_cond);
    pthread_mutex_unlock(&free_operation->completed_mutex);

    return NULL;

}

/**
 * Attempts to free the given guac_client, restricting the time taken by the
 * free handler of the guac_client to a finite number of seconds. If the free
 * handler does not complete within the time alotted, this function returns
 * and the intended free operation is left in an undefined state.
 *
 * @param client
 *     The guac_client instance to free.
 *
 * @param timeout
 *     The maximum amount of time to wait for the guac_client to be freed,
 *     in seconds.
 *
 * @return
 *     Zero if the guac_client was successfully freed within the time alotted,
 *     non-zero otherwise.
 */
static int guacd_timed_client_free(guac_client* client, int timeout) {

    pthread_t client_free_thread;

    guacd_client_free free_operation = {
        .client = client,
        .completed_cond = PTHREAD_COND_INITIALIZER,
        .completed_mutex = PTHREAD_MUTEX_INITIALIZER,
        .completed = 0
    };

    /* Get current time */
    struct timeval current_time;
    if (gettimeofday(&current_time, NULL))
        return 1;

    /* Calculate exact time that the free operation MUST complete by */
    struct timespec deadline = {
        .tv_sec  = current_time.tv_sec + timeout,
        .tv_nsec = current_time.tv_usec * 1000
    };

    /* The mutex associated with the pthread conditional and flag MUST be
     * acquired before attempting to wait for the condition */
    if (pthread_mutex_lock(&free_operation.completed_mutex))
        return 1;

    /* Free the client in a separate thread, so we can time the free operation */
    if (!pthread_create(&client_free_thread, NULL,
                guacd_client_free_thread, &free_operation)) {

        /* Wait a finite amount of time for the free operation to finish */
        (void) pthread_cond_timedwait(&free_operation.completed_cond,
                    &free_operation.completed_mutex, &deadline);
    }

    (void) pthread_mutex_unlock(&free_operation.completed_mutex);

    /* Return status of free operation */
    return !free_operation.completed;
}

/**
 * Starts protocol-specific handling on the given process by loading the client
 * plugin for that protocol. This function does NOT return. It initializes the
 * process with protocol-specific handlers and then runs until the guacd_proc's
 * fd_socket is closed, adding any file descriptors received along fd_socket as
 * new users.
 *
 * @param proc
 *     The process that any new users received along fd_socket should be added
 *     to (after the process has been initialized for the given protocol).
 *
 * @param protocol
 *     The protocol to initialize the given process for.
 */
static void guacd_exec_proc(guacd_proc* proc, const char* protocol) {

    int result = 1;
   
    /* Set process group ID to match PID */ 
    if (setpgid(0, 0)) {
        guacd_log(GUAC_LOG_ERROR, "Cannot set PGID for connection process: %s",
                strerror(errno));
        goto cleanup_process;
    }

    /* Init client for selected protocol */
    guac_client* client = proc->client;
    if (guac_client_load_plugin(client, protocol)) {

        /* Log error */
        if (guac_error == GUAC_STATUS_NOT_FOUND)
            guacd_log(GUAC_LOG_WARNING,
                    "Support for protocol \"%s\" is not installed", protocol);
        else
            guacd_log_guac_error(GUAC_LOG_ERROR,
                    "Unable to load client plugin");

        goto cleanup_client;
    }

    /* The first file descriptor is the owner */
    int owner = 1;

    /* Enable keep alive on the broadcast socket */
    guac_socket_require_keep_alive(client->socket);

    /* Add each received file descriptor as a new user */
    int received_fd;
    while ((received_fd = guacd_recv_fd(proc->fd_socket)) != -1) {

        guacd_proc_add_user(proc, received_fd, owner);

        /* Future file descriptors are not owners */
        owner = 0;

    }
    
cleanup_client:

    /* Request client to stop/disconnect */
    guac_client_stop(client);

    /* Attempt to free client cleanly */
    guacd_log(GUAC_LOG_DEBUG, "Requesting termination of client...");
    result = guacd_timed_client_free(client, GUACD_CLIENT_FREE_TIMEOUT);

    /* If client was unable to be freed, warn and forcibly kill */
    if (result) {
        guacd_log(GUAC_LOG_WARNING, "Client did not terminate in a timely "
                "manner. Forcibly terminating client and any child "
                "processes.");
        guacd_kill_current_proc_group();
    }
    else
        guacd_log(GUAC_LOG_DEBUG, "Client terminated successfully.");

    /* Verify whether children were all properly reaped */
    pid_t child_pid;
    while ((child_pid = waitpid(0, NULL, WNOHANG)) > 0) {
        guacd_log(GUAC_LOG_DEBUG, "Automatically reaped unreaped "
                "(zombie) child process with PID %i.", child_pid);
    }

    /* If running children remain, warn and forcibly kill */
    if (child_pid == 0) {
        guacd_log(GUAC_LOG_WARNING, "Client reported successful termination, "
                "but child processes remain. Forcibly terminating client and "
                "child processes.");
        guacd_kill_current_proc_group();
    }

cleanup_process:

    /* Free up all internal resources outside the client */
    close(proc->fd_socket);
    free(proc);

    exit(result);

}

guacd_proc* guacd_create_proc(const char* protocol) {

    int sockets[2];

    /* Open UNIX socket pair */
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) < 0) {
        guacd_log(GUAC_LOG_ERROR, "Error opening socket pair: %s", strerror(errno));
        return NULL;
    }

    int parent_socket = sockets[0];
    int child_socket = sockets[1];

    /* Allocate process */
    guacd_proc* proc = calloc(1, sizeof(guacd_proc));
    if (proc == NULL) {
        close(parent_socket);
        close(child_socket);
        return NULL;
    }

    /* Associate new client */
    proc->client = guac_client_alloc();
    if (proc->client == NULL) {
        guacd_log_guac_error(GUAC_LOG_ERROR, "Unable to create client");
        close(parent_socket);
        close(child_socket);
        free(proc);
        return NULL;
    }

    /* Init logging */
    proc->client->log_handler = guacd_client_log;

    /* Fork */
    proc->pid = fork();
    if (proc->pid < 0) {
        guacd_log(GUAC_LOG_ERROR, "Cannot fork child process: %s", strerror(errno));
        close(parent_socket);
        close(child_socket);
        guac_client_free(proc->client);
        free(proc);
        return NULL;
    }

    /* Child */
    else if (proc->pid == 0) {

        /* Communicate with parent */
        proc->fd_socket = parent_socket;
        close(child_socket);

        /* Start protocol-specific handling */
        guacd_exec_proc(proc, protocol);

    }

    /* Parent */
    else {

        /* Communicate with child */
        proc->fd_socket = child_socket;
        close(parent_socket);

    }

    return proc;

}

void guacd_proc_stop(guacd_proc* proc) {

    /* Signal client to stop */
    guac_client_stop(proc->client);

    /* Shutdown socket - in-progress recvmsg() will not fail otherwise */
    if (shutdown(proc->fd_socket, SHUT_RDWR) == -1)
        guacd_log(GUAC_LOG_ERROR, "Unable to shutdown internal socket for "
                "connection %s. Corresponding process may remain running but "
                "inactive.", proc->client->connection_id);

    /* Clean up our end of the socket */
    close(proc->fd_socket);

}

