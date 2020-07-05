#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <webfuse/webfuse.h>
#include <webfuse_provider/webfuse_provider.h>
#include <libwebsockets.h>

#define FILE_INODE 2
#define FILE_NAME "file.bin"
#define FILE_SIZE  (2 * 1024)

struct wf_mountpoint *
create_mountpoint(
    char const * filesystem,
    void * user_data)
{
    (void) user_data;
    return wf_mountpoint_create("/tmp/wf_bench");
}

static struct lws_context * context = NULL; 
static bool is_shutdown_requested = false;
static char * contents = NULL;

static void on_shutdown_requested(int signal)
{
    (void) signal;
    is_shutdown_requested = true;
    lws_cancel_service(context);
}


static void on_connected(
    void * user_data)
{
    (void) user_data;
    puts("connected");
}

static void on_disconnected(
    void * user_data)
{
    (void) user_data;

    if (!is_shutdown_requested)
    {
        fprintf(stderr, "error: failed to connect\n");
        is_shutdown_requested = true;
        lws_cancel_service(context);
    }
}

static void on_lookup(
    struct wfp_request * request,
    ino_t parent,
    char const * name,
    void * user_data)
{
    (void) user_data;
    struct stat info;
    memset(&info, 0, sizeof(struct stat));

    if ((1 == parent) && (0 == strcmp(name, FILE_NAME)))
    {
        info.st_ino = FILE_INODE;
        info.st_mode = 0644 | S_IFREG;
        info.st_size = FILE_SIZE;
        wfp_respond_lookup(request, &info);
    }
    else
    {
        wfp_respond_error(request, WFP_BAD);
    }

}

static void on_getattr(
    struct wfp_request * request,
    ino_t inode,
    void * user_data)
{
    (void) user_data;
    struct stat info;
    memset(&info, 0, sizeof(struct stat));

    switch (inode)
    {
        case 1:
            info.st_ino = 1;
            info.st_mode = 0644 | S_IFDIR;
            wfp_respond_getattr(request, &info);
            break;
        case FILE_INODE:
            info.st_ino = FILE_INODE;
            info.st_mode = 0644 | S_IFREG;
            info.st_size = FILE_SIZE;
            wfp_respond_getattr(request, &info);
            break;
        default:
            wfp_respond_error(request, WFP_BAD_NOENTRY);
            break;
    }
}

static void on_readdir(
    struct wfp_request * request,
    ino_t directory,
    void * user_data)
{
    (void) user_data;
    struct wfp_dirbuffer * buffer = wfp_dirbuffer_create();
    wfp_dirbuffer_add(buffer, ".", directory);
    wfp_dirbuffer_add(buffer, "..", directory);

    switch(directory)
    {
        case 1:
            wfp_dirbuffer_add(buffer, FILE_NAME, FILE_INODE);
            wfp_respond_readdir(request, buffer);
            break;
        default:
            wfp_respond_error(request, WFP_BAD_NOENTRY);
            break;
    }

    wfp_dirbuffer_dispose(buffer);
}

static void on_open(
    struct wfp_request * request,
    ino_t inode,
    int flags,
    void * user_data)
{
    (void) user_data;
    (void) flags;

    switch (inode)
    {
        case 1:
            wfp_respond_error(request, WFP_BAD_ACCESS_DENIED);
            break;
        case FILE_INODE:
            wfp_respond_open(request, 42);
            break;
        default:
            wfp_respond_error(request, WFP_BAD_NOENTRY);
    }
}

static void on_read(
    struct wfp_request * request,
    ino_t inode,
    uint32_t handle,
    size_t offset,
    size_t length,
    void * user_data)
{
    (void) user_data;
    (void) handle;

    if (inode == FILE_INODE)
    {
        size_t remaining = (offset < FILE_SIZE) ? FILE_SIZE - offset : 0;
        size_t count = (length < remaining) ? length : remaining;

        wfp_respond_read(request, &contents[offset], count);
    }
    else
    {
        wfp_respond_error(request, WFP_BAD_NOENTRY);
    }
}


int main(int argc, char * argv[])
{
    contents = malloc(FILE_SIZE);
    memset(contents, 0, FILE_SIZE);

    struct wf_server_protocol * server = wf_server_protocol_create(
        create_mountpoint, NULL);
    struct wfp_client_config * config = wfp_client_config_create();
    wfp_client_config_set_onconnected(config, on_connected);
    wfp_client_config_set_ondisconnected(config, on_disconnected);
    wfp_client_config_set_onlookup(config, on_lookup);
    wfp_client_config_set_ongetattr(config, on_getattr);
    wfp_client_config_set_onreaddir(config, on_readdir);
    wfp_client_config_set_onopen(config, on_open);
    wfp_client_config_set_onread(config, on_read);
    struct wfp_client_protocol * client = wfp_client_protocol_create(config);

    struct lws_protocols protocols[3];
    memset(protocols, 0,3 * sizeof(struct lws_protocols));
    wf_server_protocol_init_lws(server, &protocols[0]);
    wfp_client_protocol_init_lws(client, &protocols[1]);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(struct lws_context_creation_info));
    info.port = 0;
    info.protocols = protocols;
    info.vhost_name = "localhost";
    info.ws_ping_pong_interval = 10;
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
    info.options |= LWS_SERVER_OPTION_EXPLICIT_VHOSTS;

    context = lws_create_context(&info);
    struct lws_vhost * vhost = lws_create_vhost(context, &info);
    int port = lws_get_vhost_port(vhost);

    char url[80];
    snprintf(url, 80, "ws://localhost:%d/", port);
    wfp_client_protocol_connect(client, context, url);

    signal(SIGINT, &on_shutdown_requested);
    while (!is_shutdown_requested)
    {
        lws_service(context, 0);
    }

    lws_context_destroy(context);
    wfp_client_protocol_dispose(client);
    wfp_client_config_dispose(config);
    wf_server_protocol_dispose(server);
    free(contents);
    context = NULL;
    contents = NULL;

    return EXIT_SUCCESS;
}