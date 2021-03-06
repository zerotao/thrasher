#include "thrasher.h"
#include "version.h"

extern char    *process_name;
extern uint32_t uri_check;
extern uint32_t site_check;
extern uint32_t addr_check;
extern char    *bind_addr;
extern uint16_t bind_port;
extern uint32_t soft_block_timeout;
extern block_ratio_t site_ratio;
extern block_ratio_t uri_ratio;
extern block_ratio_t addr_ratio;
extern int      server_port;
extern uint32_t qps;
extern uint32_t qps_last;
extern uint64_t total_blocked_connections;
extern uint64_t total_queries;
extern int      rbl_queries;
extern int      rbl_max_queries;
extern char    *rbl_zone;
extern int      rbl_negcache_timeout;
extern char    *rbl_ns;
extern uint32_t connection_timeout;

extern block_ratio_t minimum_random_ratio;
extern block_ratio_t maximum_random_ratio;
extern uint32_t recently_blocked_timeout;


extern GTree   *current_blocks;
extern GSList  *current_connections;
extern GTree   *recently_blocked;

gboolean
fill_http_blocks(void *key, blocked_node_t * val, struct evbuffer *buf)
{
    char           *blockedaddr;
    char           *triggeraddr;

    blockedaddr = strdup(inet_ntoa(*(struct in_addr *) &val->saddr));

    triggeraddr =
        strdup(inet_ntoa(*(struct in_addr *) &val->first_seen_addr));

    if (blockedaddr && triggeraddr)
        evbuffer_add_printf(buf, "%-15s %-15s %-15d\n",
                            blockedaddr, triggeraddr, val->count);

    if (blockedaddr)
        free(blockedaddr);
    if (triggeraddr)
        free(triggeraddr);

    return FALSE;
}

void
fill_current_connections(client_conn_t * conn, struct evbuffer *buf)
{
    if (conn == NULL)
        return;

    evbuffer_add_printf(buf, "    %-15s %-5d %-15s\n",
                        inet_ntoa(*(struct in_addr *) &conn->conn_addr),
                        ntohs(conn->conn_port), "ESTABLISHED");
}

void
httpd_put_hips(struct evhttp_request *req, void *args)
{
    struct evbuffer *buf;

    buf = evbuffer_new();

    evbuffer_add_printf(buf, "%-15s %-15s %-15s\n",
                        "Blocked IP", "Triggered By", "Count");

    g_tree_foreach(current_blocks, (GTraverseFunc) fill_http_blocks, buf);

    evhttp_send_reply(req, HTTP_OK, "OK", buf);
    evbuffer_free(buf);
}

void
httpd_put_connections(struct evhttp_request *req, void *args)
{
    struct evbuffer *buf;

    buf = evbuffer_new();

    evbuffer_add_printf(buf, "\nCurrent active connections\n");
    evbuffer_add_printf(buf,
                        "    %-15s %-5s %-15s\n", "Addr", "Port", "State");

    g_slist_foreach(current_connections,
                    (GFunc) fill_current_connections, buf);

    evhttp_send_reply(req, HTTP_OK, "OK", buf);
    evbuffer_free(buf);
}

void
httpd_put_config(struct evhttp_request *req, void *args)
{
    struct evbuffer *buf;

    buf = evbuffer_new();

    evbuffer_add_printf(buf, "Thrashd version: %s (%s) [%s]\n", VERSION,
                        VERSION_NAME, process_name);

    evbuffer_add_printf(buf, "Running configuration\n\n");
    evbuffer_add_printf(buf, "  URI Check Enabled:     %s\n",
                        uri_check ? "yes" : "no");
    evbuffer_add_printf(buf, "  Host Check Enabled:    %s\n",
                        site_check ? "yes" : "no");
    evbuffer_add_printf(buf, "  Addr Check Enabled:    %s\n",
                        addr_check ? "yes" : "no");
    evbuffer_add_printf(buf, "  Sliding Ratio Enabled: %s\n",
	                recently_blocked ? "yes" : "no");
    evbuffer_add_printf(buf, "  RBL Enabled:           %s\n",
	                rbl_zone ? "yes":"no");

    evbuffer_add_printf(buf, "  Bind addr:             %s\n", bind_addr);
    evbuffer_add_printf(buf, "  Bind port:             %d\n", bind_port);
    evbuffer_add_printf(buf, "  Client Idle Timeout:   %u\n", connection_timeout);
    evbuffer_add_printf(buf, "  Soft block timeout:    %d\n\n", soft_block_timeout);

    evbuffer_add_printf(buf, "  RBL Zone:              %s\n", rbl_zone?rbl_zone:"NULL");
    evbuffer_add_printf(buf, "  RBL Nameserver:        %s\n", rbl_ns?rbl_ns:"NULL");
    evbuffer_add_printf(buf, "  RBL Max Async Queries: %d\n", rbl_max_queries);
    evbuffer_add_printf(buf, "  RBL NegCache Timeout:  %d\n\n", rbl_negcache_timeout);

    evbuffer_add_printf(buf,
                        "  Host block ratio: %d hits over %d seconds\n",
                        site_ratio.num_connections, site_ratio.timelimit);
    evbuffer_add_printf(buf,
                        "  URI block ratio:  %d hits over %d seconds\n",
                        uri_ratio.num_connections, uri_ratio.timelimit);
    evbuffer_add_printf(buf,
                        "  ADDR block ratio: %d hits over %d seconds\n\n",
                        addr_ratio.num_connections, addr_ratio.timelimit);

    evbuffer_add_printf(buf,
	                "  Sliding Ratio MINIMUM: %d hits over %d seconds\n",
			minimum_random_ratio.num_connections,
			minimum_random_ratio.timelimit);
    evbuffer_add_printf(buf,
	                "  Sliding Ratio MAXIMUM: %d hits over %d seconds\n",
			maximum_random_ratio.num_connections,
			maximum_random_ratio.timelimit);

    evbuffer_add_printf(buf,
	                "  Sliding Ratio Recently Blocked Timeout: %u\n\n",
			recently_blocked_timeout);

    evbuffer_add_printf(buf,
	                "%d clients currently connected\n",
			g_slist_length(current_connections)-1);

    evbuffer_add_printf(buf,
                        "%d addresses currently in hold-down (%u qps)\n",
                        g_tree_nnodes(current_blocks), qps_last);
    evbuffer_add_printf(buf, "Total connections blocked: %llu\n",
                        total_blocked_connections);
    evbuffer_add_printf(buf, "Total queries recv: %llu\n", total_queries);
    evbuffer_add_printf(buf, "DNS Query backlog: %d/%d\n", rbl_queries,
                        rbl_max_queries);

    evhttp_send_reply(req, HTTP_OK, "OK", buf);
    evbuffer_free(buf);
}

void
httpd_driver(struct evhttp_request *req, void *arg)
{
    struct evbuffer *buf;

    buf = evbuffer_new();

    evbuffer_add_printf(buf, "Thrashd version: %s [%s]\n", VERSION,
                        process_name);
    evhttp_send_reply(req, HTTP_OK, "OK", buf);
    evbuffer_free(buf);
}

int
webserver_init(void)
{
    struct evhttp  *httpd;
    httpd = evhttp_start(bind_addr, server_port);

    if (httpd == NULL)
        return -1;

    evhttp_set_cb(httpd, "/holddowns", httpd_put_hips, NULL);
    evhttp_set_cb(httpd, "/config", httpd_put_config, NULL);
    evhttp_set_cb(httpd, "/connections", httpd_put_connections, NULL);
    evhttp_set_gencb(httpd, httpd_driver, NULL);
    return 0;
}
