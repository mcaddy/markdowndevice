/*
 * Minimal HTTP/1.1 server on the lwIP raw TCP API.
 *
 * Handles the markdown editor's routes: a server-rendered home page and editor
 * (so they work with JavaScript disabled — important for e-reader browsers),
 * plus static assets, a JSON listing, file download, save, upload and delete.
 */
#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

/* Start listening on TCP port 80. Call after lwIP is initialised. */
void http_server_init(void);

#endif /* HTTP_SERVER_H_ */
