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
#include <dlfcn.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

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
                    "Support for protocol \"%s\" is not installed: %s", protocol, guac_error_message);
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
    proc->client = guac_client_alloc(0);
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

#ifdef HAVE_EXECINFO_H
/**
 * Maximum number of stack frames to capture in backtrace.
 */
#define MAX_BACKTRACE_FRAMES 128
#endif

/**
 * Signal handler for capturing stack traces on crashes.
 * This handler will log a backtrace and signal information before terminating.
 *
 * @param signum
 *     The signal number that triggered this handler.
 *
 * @param info
 *     Signal information structure (may be NULL if not available).
 *
 * @param context
 *     Signal context (unused but required by sigaction).
 */
static void guacd_crash_handler(int signum, siginfo_t* info, void* context) {

    const char* signal_name;

    /* Map signal number to human-readable name */
    switch (signum) {
        case SIGSEGV: signal_name = "SIGSEGV (Segmentation fault)"; break;
        case SIGABRT: signal_name = "SIGABRT (Abort)"; break;
        case SIGBUS:  signal_name = "SIGBUS (Bus error)"; break;
        case SIGILL:  signal_name = "SIGILL (Illegal instruction)"; break;
        case SIGFPE:  signal_name = "SIGFPE (Floating point exception)"; break;
        default:      signal_name = "Unknown signal"; break;
    }

    /* Log crash header with signal information */
    guacd_log(GUAC_LOG_ERROR,
            "========================================");
    guacd_log(GUAC_LOG_ERROR,
            "FATAL: Child process crashed with signal %d: %s",
            signum, signal_name);

    /* Log additional signal information if available */
    if (info != NULL) {
        guacd_log(GUAC_LOG_ERROR,
                "Signal code: %d, Fault address: %p, PID: %d",
                info->si_code, info->si_addr, getpid());
    }

#ifdef HAVE_EXECINFO_H
    /* Capture and log backtrace if available */
    void* trace[MAX_BACKTRACE_FRAMES];
    int trace_size = backtrace(trace, MAX_BACKTRACE_FRAMES);

    guacd_log(GUAC_LOG_ERROR,
            "Stack trace (%d frames):", trace_size);

    /* Use backtrace_symbols_fd to write directly to stderr
     * This is safer than backtrace_symbols() as it doesn't use malloc */
    backtrace_symbols_fd(trace, trace_size, STDERR_FILENO);
#else
    guacd_log(GUAC_LOG_ERROR,
            "Stack trace: Not available (execinfo.h not found during build)");
    guacd_log(GUAC_LOG_ERROR,
            "To enable stack traces: Install libexecinfo-dev and rebuild");
    guacd_log(GUAC_LOG_ERROR,
            "  Alpine Linux: apk add libexecinfo-dev libexecinfo");
    guacd_log(GUAC_LOG_ERROR,
            "Or analyze core dump with: gdb /path/to/guacd core.<pid>");
#endif

    guacd_log(GUAC_LOG_ERROR,
            "========================================");

    /* Re-raise signal with default handler to generate core dump if enabled */
    signal(signum, SIG_DFL);
    raise(signum);
}

/**
 * Installs signal handlers to capture stack traces on crashes.
 * Should be called early in the child process initialization.
 *
 * @return
 *     Zero on success, non-zero if signal handler installation failed.
 */
static int guacd_install_crash_handlers(void) {

    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));

    /* Use SA_SIGINFO to get extended signal information */
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sa.sa_sigaction = guacd_crash_handler;
    sigemptyset(&sa.sa_mask);

    /* Install handlers for common crash signals */
    if (sigaction(SIGSEGV, &sa, NULL) != 0) {
        guacd_log(GUAC_LOG_WARNING,
                "Failed to install SIGSEGV handler: %s", strerror(errno));
        return 1;
    }

    if (sigaction(SIGABRT, &sa, NULL) != 0) {
        guacd_log(GUAC_LOG_WARNING,
                "Failed to install SIGABRT handler: %s", strerror(errno));
        return 1;
    }

    if (sigaction(SIGBUS, &sa, NULL) != 0) {
        guacd_log(GUAC_LOG_WARNING,
                "Failed to install SIGBUS handler: %s", strerror(errno));
        return 1;
    }

    if (sigaction(SIGILL, &sa, NULL) != 0) {
        guacd_log(GUAC_LOG_WARNING,
                "Failed to install SIGILL handler: %s", strerror(errno));
        return 1;
    }

    if (sigaction(SIGFPE, &sa, NULL) != 0) {
        guacd_log(GUAC_LOG_WARNING,
                "Failed to install SIGFPE handler: %s", strerror(errno));
        return 1;
    }

#ifdef HAVE_EXECINFO_H
    guacd_log(GUAC_LOG_DEBUG,
            "Crash signal handlers installed successfully (with backtrace support)");
#else
    guacd_log(GUAC_LOG_DEBUG,
            "Crash signal handlers installed (without backtrace - execinfo.h not available)");
    guacd_log(GUAC_LOG_DEBUG,
            "Stack traces disabled. To enable: Install libexecinfo-dev and rebuild");
#endif

    return 0;
}

/**
 * Returns a human-readable name for a given signal number.
 *
 * @param signum
 *     The signal number.
 *
 * @return
 *     A string describing the signal, or "Unknown signal" if not recognized.
 */
static const char* guacd_get_signal_name(int signum) {
    switch (signum) {
        case SIGHUP:    return "SIGHUP (Hangup)";
        case SIGINT:    return "SIGINT (Interrupt)";
        case SIGQUIT:   return "SIGQUIT (Quit)";
        case SIGILL:    return "SIGILL (Illegal instruction)";
        case SIGTRAP:   return "SIGTRAP (Trace/breakpoint trap)";
        case SIGABRT:   return "SIGABRT (Abort)";
        case SIGBUS:    return "SIGBUS (Bus error)";
        case SIGFPE:    return "SIGFPE (Floating point exception)";
        case SIGKILL:   return "SIGKILL (Killed)";
        case SIGSEGV:   return "SIGSEGV (Segmentation fault)";
        case SIGPIPE:   return "SIGPIPE (Broken pipe)";
        case SIGALRM:   return "SIGALRM (Alarm clock)";
        case SIGTERM:   return "SIGTERM (Terminated)";
        default:        return "Unknown signal";
    }
}

void guacd_exec_proc_native(guacd_proc* proc, int client_fd)
{
    guacd_log(GUAC_LOG_DEBUG,
            "[CHILD-INIT] Child process started, PID=%d", getpid());

    /* Install crash signal handlers for stack trace capture */
    if (guacd_install_crash_handlers() != 0) {
        guacd_log(GUAC_LOG_WARNING,
                "[CHILD-INIT] Failed to install crash handlers, continuing anyway");
    }

    /* Set process group ID to match PID */
    guacd_log(GUAC_LOG_DEBUG, "[CHILD-INIT] Setting process group ID");
    if (setpgid(0, 0)) {
        guacd_log(GUAC_LOG_ERROR,
                "[CHILD-INIT] Cannot set PGID for connection process: %s",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Reference to dlopen()'d plugin */
    void* client_plugin_handle;

    /* Pluggable client */
    const char* protocol_lib = GUAC_PROTOCOL_LIBRARY_PREFIX "rdp" GUAC_PROTOCOL_LIBRARY_SUFFIX;

    /* Type-pun for the sake of dlsym() - cannot typecast a void* to a function
     * pointer otherwise */
    union {
        guac_rdp_proxy_connect_handler* proxy_connect;
        void* obj;
    } alias;

    guacd_log(GUAC_LOG_INFO, "[PLUGIN-LOAD] Loading client plugin: %s", protocol_lib);

    /* Load client plugin */
    guacd_log(GUAC_LOG_DEBUG, "[PLUGIN-LOAD] Calling dlopen() for RDP plugin");
    client_plugin_handle = dlopen(protocol_lib, RTLD_LAZY);
    if (!client_plugin_handle) {
        guac_error = GUAC_STATUS_NOT_FOUND;
        guac_error_message = dlerror();
        guacd_log(GUAC_LOG_ERROR,
                "[PLUGIN-LOAD] Unable to load client plugin \"%s\": %d, %s",
                protocol_lib, guac_error, guac_error_message);
        guacd_log(GUAC_LOG_ERROR,
                "[PLUGIN-LOAD] Check that the RDP plugin is installed and library path is correct");
        sleep(2);
        exit(EXIT_FAILURE);
    }
    guacd_log(GUAC_LOG_DEBUG, "[PLUGIN-LOAD] Successfully opened plugin library");

    dlerror(); /* Clear errors */

    guacd_log(GUAC_LOG_INFO, "[PLUGIN-LOAD] Looking up guac_rdp_proxy_connect symbol");

    /* Get init function */
    alias.obj = dlsym(client_plugin_handle, "guac_rdp_proxy_connect");

    /* Fail if cannot find guac_rdp_proxy_connect */
    if (dlerror() != NULL) {
        guac_error = GUAC_STATUS_INTERNAL_ERROR;
        guac_error_message = dlerror();
        guacd_log(GUAC_LOG_ERROR,
                "[PLUGIN-LOAD] Symbol 'guac_rdp_proxy_connect' not found: %s",
                guac_error_message);
        dlclose(client_plugin_handle);
        exit(EXIT_FAILURE);
    }
    guacd_log(GUAC_LOG_DEBUG, "[PLUGIN-LOAD] Symbol resolved successfully");

    /* Init client */
    proc->client->__plugin_handle = client_plugin_handle;

    guacd_log(GUAC_LOG_INFO, "[CONNECTION] Starting RDP proxy connection handler");
    guacd_log(GUAC_LOG_DEBUG, "[CONNECTION] Client FD=%d, Process PID=%d", client_fd, getpid());

    int ret = alias.proxy_connect(proc->client, client_fd);
    if (ret) {
        guacd_log(GUAC_LOG_ERROR,
                "[CONNECTION] guac_rdp_proxy_connect failed with return code: %d", ret);
        guacd_log(GUAC_LOG_ERROR,
                "[CONNECTION] Connection handler terminated abnormally");
    } else {
        guacd_log(GUAC_LOG_INFO,
                "[CONNECTION] guac_rdp_proxy_connect completed successfully");
    }

    guacd_log(GUAC_LOG_DEBUG, "[CHILD-EXIT] Child process exiting with code: %d", ret);
    exit(ret);
}

void guacd_create_proc_native(guac_client* client, int client_fd) {

    /* Allocate process */
    guacd_proc* proc = calloc(1, sizeof(guacd_proc));
    if (proc == NULL) {
        guacd_log(GUAC_LOG_ERROR, "[PARENT] Failed to allocate memory for process structure");
        return;
    }
    proc->fd_socket = -1;
    proc->client = client;

    /* Fork */
    proc->pid = fork();
    if (proc->pid < 0) {
        guacd_log(GUAC_LOG_ERROR, "[PARENT] Cannot fork child process: %s", strerror(errno));
        guac_client_free(proc->client);
        free(proc);
        return;
    }

    /* Child */
    if (proc->pid == 0) {
        guacd_exec_proc_native(proc, client_fd);
    }

    /* Parent, waiting for the child process */
    guacd_log(GUAC_LOG_DEBUG, "[PARENT] Waiting for child process PID=%d", proc->pid);

    int status;
    const int wpid = waitpid(proc->pid, &status, 0);

    if (wpid == -1) {
        guacd_log(GUAC_LOG_ERROR,
                "[PARENT] waitpid() failed for child PID=%d: %s",
                proc->pid, strerror(errno));
        goto proc_cleanup;
    }

    /* Analyze child process termination status */
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) {
            guacd_log(GUAC_LOG_INFO,
                    "[PARENT] Child process PID=%d exited successfully (code 0)",
                    proc->pid);
        } else {
            guacd_log(GUAC_LOG_ERROR,
                    "[PARENT] Child process PID=%d exited with error code %d",
                    proc->pid, exit_code);
            guacd_log(GUAC_LOG_ERROR,
                    "[PARENT] Check child process logs above for detailed error information");
        }
    } else if (WIFSIGNALED(status)) {
        int signum = WTERMSIG(status);
        const char* signal_name = guacd_get_signal_name(signum);

        guacd_log(GUAC_LOG_ERROR,
                "========================================");
        guacd_log(GUAC_LOG_ERROR,
                "[PARENT] Child process PID=%d terminated by signal %d: %s",
                proc->pid, signum, signal_name);

#ifdef WCOREDUMP
        if (WCOREDUMP(status)) {
            guacd_log(GUAC_LOG_ERROR,
                    "[PARENT] Core dump was generated");
            guacd_log(GUAC_LOG_ERROR,
                    "[PARENT] Analyze core dump with: gdb /path/to/guacd core.%d",
                    proc->pid);
        }
#endif

        /* Provide specific guidance based on signal type */
        switch (signum) {
            case SIGSEGV:
            case SIGBUS:
                guacd_log(GUAC_LOG_ERROR,
                        "[PARENT] Memory access error detected. Check stack trace in logs above.");
                guacd_log(GUAC_LOG_ERROR,
                        "[PARENT] This may indicate a null pointer dereference or buffer overflow.");
                break;
            case SIGABRT:
                guacd_log(GUAC_LOG_ERROR,
                        "[PARENT] Process aborted. This may indicate an assertion failure or fatal error.");
                break;
            case SIGILL:
                guacd_log(GUAC_LOG_ERROR,
                        "[PARENT] Illegal instruction. This may indicate corrupted code or architecture mismatch.");
                break;
            case SIGFPE:
                guacd_log(GUAC_LOG_ERROR,
                        "[PARENT] Floating point exception. Check for division by zero or invalid math operations.");
                break;
            case SIGPIPE:
                guacd_log(GUAC_LOG_ERROR,
                        "[PARENT] Broken pipe. Remote connection may have closed unexpectedly.");
                break;
        }

        guacd_log(GUAC_LOG_ERROR,
                "[PARENT] Enable core dumps with: ulimit -c unlimited");
        guacd_log(GUAC_LOG_ERROR,
                "========================================");

    } else if (WIFSTOPPED(status)) {
        int signum = WSTOPSIG(status);
        const char* signal_name = guacd_get_signal_name(signum);
        guacd_log(GUAC_LOG_WARNING,
                "[PARENT] Child process PID=%d stopped by signal %d: %s",
                proc->pid, signum, signal_name);
    } else {
        /* Non-standard case -- may never happen */
        guacd_log(GUAC_LOG_ERROR,
                "[PARENT] Child process PID=%d terminated with unexpected status: 0x%x",
                proc->pid, status);
    }

proc_cleanup:
    guacd_log(GUAC_LOG_DEBUG, "[PARENT] Cleaning up process resources");
    guacd_proc_stop(proc);
    free(proc);
}

void guacd_proc_stop(guacd_proc* proc) {

    /* Signal client to stop */
    guac_client_stop(proc->client);

    if (proc->fd_socket != -1)
    {
        /* Shutdown socket - in-progress recvmsg() will not fail otherwise */
        if (shutdown(proc->fd_socket, SHUT_RDWR) == -1)
            guacd_log(GUAC_LOG_ERROR, "Unable to shutdown internal socket for "
                    "connection %s. Corresponding process may remain running but "
                    "inactive.", proc->client->connection_id);

        /* Clean up our end of the socket */
        close(proc->fd_socket);
    }

}

