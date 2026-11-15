/*
 * Minimal HTTP/1.1 server on lwIP's raw TCP API. See http_server.h.
 *
 * One connection = one request (Connection: close). Requests are parsed
 * incrementally as bytes arrive; responses are streamed from either an
 * in-RAM buffer (static assets / generated HTML+JSON) or, for downloads,
 * directly from a LittleFS file so we never hold a whole document in RAM
 * on the response side.
 */
#include "http_server.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#include "fs/docstore.h"
#include "web_assets.h"

#define HTTP_PORT           80
#define HDR_CAP             2048          /* max request header block */
#define HTTP_MAX_BODY       (80u * 1024u) /* max POST body we will buffer */
#define EDIT_INLINE_MAX     (32u * 1024u) /* max doc size prefilled into <textarea> */
#define FILE_CHUNK          512

typedef enum { M_GET, M_POST, M_OTHER } http_method_t;
typedef enum { SRC_NONE, SRC_MEM, SRC_FILE } resp_src_t;

typedef struct {
  struct tcp_pcb *pcb;

  /* request */
  char     hdr[HDR_CAP];
  int      hdr_len;
  int      headers_done;
  http_method_t method;
  char     path[256];
  char     query[256];
  long     content_length;
  char     content_type[128];

  /* request body */
  uint8_t *body;
  uint32_t body_cap;
  uint32_t body_len;
  int      request_ready;   /* full request received, ready to dispatch */
  int      responded;

  /* response header */
  char     rhdr[352];
  int      rhdr_len;
  int      rhdr_off;

  /* response body source */
  resp_src_t src;
  const uint8_t *mem;
  uint32_t mem_len;
  uint32_t mem_off;
  int      mem_owned;

  lfs_file_t file;
  int        file_open;
  uint32_t   file_remaining;

  int      closing;
} http_conn_t;

/* ---- small helpers ------------------------------------------------------ */

static void *xmemmem(const void *hay, size_t haylen, const void *need, size_t needlen) {
  if (needlen == 0 || haylen < needlen) return NULL;
  const uint8_t *h = (const uint8_t *) hay;
  for (size_t i = 0; i + needlen <= haylen; i++) {
    if (h[i] == ((const uint8_t *) need)[0] && memcmp(h + i, need, needlen) == 0)
      return (void *) (h + i);
  }
  return NULL;
}

static int ci_starts_with(const char *s, const char *prefix) {
  while (*prefix) {
    char a = *s++, b = *prefix++;
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return 0;
  }
  return 1;
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* Percent/plus decode src[0..srclen) into dst (NUL-terminated). Caps at dstcap-1. */
static void url_decode(char *dst, size_t dstcap, const char *src, size_t srclen) {
  size_t o = 0;
  for (size_t i = 0; i < srclen && o + 1 < dstcap; i++) {
    char c = src[i];
    if (c == '+') {
      dst[o++] = ' ';
    } else if (c == '%' && i + 2 < srclen) {
      int hi = hexval(src[i + 1]), lo = hexval(src[i + 2]);
      if (hi >= 0 && lo >= 0) { dst[o++] = (char) ((hi << 4) | lo); i += 2; }
      else dst[o++] = c;
    } else {
      dst[o++] = c;
    }
  }
  dst[o] = '\0';
}

/* Find key=value in an application/x-www-form-urlencoded string (query or body). */
static int form_get(const char *form, size_t form_len, const char *key,
                    char *out, size_t outcap) {
  size_t klen = strlen(key);
  size_t i = 0;
  while (i < form_len) {
    size_t start = i;
    while (i < form_len && form[i] != '&') i++;
    size_t seglen = i - start;
    const char *seg = form + start;
    if (seglen > klen && seg[klen] == '=' && memcmp(seg, key, klen) == 0) {
      url_decode(out, outcap, seg + klen + 1, seglen - klen - 1);
      return 0;
    }
    if (i < form_len) i++;   /* skip '&' */
  }
  return -1;
}

/* ---- dynamic string builder (for generated HTML/JSON) ------------------- */
typedef struct { char *buf; size_t len, cap; int oom; } sb_t;

static void sb_init(sb_t *s) { s->buf = NULL; s->len = 0; s->cap = 0; s->oom = 0; }

static int sb_reserve(sb_t *s, size_t extra) {
  if (s->oom) return -1;
  if (s->len + extra + 1 <= s->cap) return 0;
  size_t ncap = s->cap ? s->cap : 512;
  while (ncap < s->len + extra + 1) ncap *= 2;
  char *nb = (char *) realloc(s->buf, ncap);
  if (!nb) { s->oom = 1; return -1; }
  s->buf = nb; s->cap = ncap;
  return 0;
}

static void sb_putn(sb_t *s, const char *p, size_t n) {
  if (sb_reserve(s, n)) return;
  memcpy(s->buf + s->len, p, n);
  s->len += n;
  s->buf[s->len] = '\0';
}
static void sb_puts(sb_t *s, const char *p) { sb_putn(s, p, strlen(p)); }

/* Append text HTML-escaped (& < > " '). */
static void sb_put_esc(sb_t *s, const char *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c = p[i];
    switch (c) {
      case '&': sb_puts(s, "&amp;");  break;
      case '<': sb_puts(s, "&lt;");   break;
      case '>': sb_puts(s, "&gt;");   break;
      case '"': sb_puts(s, "&quot;"); break;
      case '\'':sb_puts(s, "&#39;");  break;
      default:  sb_putn(s, &c, 1);    break;
    }
  }
}

/* ---- connection lifecycle ----------------------------------------------- */

static void conn_free_response(http_conn_t *c) {
  if (c->file_open) { docstore_close(&c->file); c->file_open = 0; }
  if (c->mem_owned && c->mem) { free((void *) c->mem); }
  c->mem = NULL; c->mem_owned = 0; c->src = SRC_NONE;
}

static void conn_close(http_conn_t *c) {
  struct tcp_pcb *pcb = c->pcb;
  conn_free_response(c);
  if (c->body) { free(c->body); c->body = NULL; }
  if (pcb) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    if (tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
    }
  }
  free(c);
}

/* ---- response builders -------------------------------------------------- */

static void set_header(http_conn_t *c, const char *status, const char *ctype,
                       uint32_t content_len, const char *extra) {
  c->rhdr_len = snprintf(c->rhdr, sizeof(c->rhdr),
      "HTTP/1.1 %s\r\n"
      "Server: markdowndevice\r\n"
      "Connection: close\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %lu\r\n"
      "%s"
      "\r\n",
      status, ctype, (unsigned long) content_len, extra ? extra : "");
  if (c->rhdr_len < 0 || c->rhdr_len >= (int) sizeof(c->rhdr)) {
    /* header overflow — fall back to a minimal 500 */
    c->rhdr_len = snprintf(c->rhdr, sizeof(c->rhdr),
        "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    content_len = 0;
  }
  c->rhdr_off = 0;
}

static void respond_mem(http_conn_t *c, const char *status, const char *ctype,
                        const uint8_t *data, uint32_t len, int owned, const char *extra) {
  conn_free_response(c);
  set_header(c, status, ctype, len, extra);
  c->src = SRC_MEM;
  c->mem = data; c->mem_len = len; c->mem_off = 0; c->mem_owned = owned;
}

/* Take ownership of an sb buffer (or emit 500 on OOM). */
static void respond_sb(http_conn_t *c, const char *status, const char *ctype, sb_t *s) {
  if (s->oom) {
    if (s->buf) free(s->buf);
    static const char msg[] = "Out of memory";
    respond_mem(c, "500 Internal Server Error", "text/plain",
                (const uint8_t *) msg, sizeof(msg) - 1, 0, NULL);
    return;
  }
  respond_mem(c, status, ctype, (const uint8_t *) s->buf, (uint32_t) s->len, 1, NULL);
}

static void respond_text(http_conn_t *c, const char *status, const char *text) {
  respond_mem(c, status, "text/plain; charset=utf-8",
              (const uint8_t *) text, (uint32_t) strlen(text), 0, NULL);
}

static void respond_redirect(http_conn_t *c, const char *location) {
  char extra[300];
  snprintf(extra, sizeof(extra), "Location: %s\r\n", location);
  respond_mem(c, "303 See Other", "text/plain", (const uint8_t *) "", 0, 0, extra);
}

/* ---- page rendering ----------------------------------------------------- */

static const char PAGE_HEAD[] =
  "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
  "<title>Markdown Editor</title>"
  "<link rel=\"stylesheet\" href=\"/style.css\"></head><body>";
static const char PAGE_FOOT[] = "</body></html>";

typedef struct { sb_t *sb; int count; } list_ctx_t;

static int home_list_cb(const char *name, uint32_t size, void *arg) {
  list_ctx_t *lc = (list_ctx_t *) arg;
  sb_t *s = lc->sb;
  lc->count++;
  sb_puts(s, "<li><a class=\"doc\" href=\"/edit?name=");
  sb_put_esc(s, name, strlen(name));   /* also fine for attribute context */
  sb_puts(s, "\">");
  sb_put_esc(s, name, strlen(name));
  sb_puts(s, "</a> <span class=\"sz\">");
  char num[16]; snprintf(num, sizeof(num), "%lu B", (unsigned long) size);
  sb_puts(s, num);
  sb_puts(s, "</span> <a class=\"btn\" href=\"/doc/");
  sb_put_esc(s, name, strlen(name));
  sb_puts(s, "\">download</a> "
             "<form class=\"inline\" method=\"post\" action=\"/delete\" "
             "onsubmit=\"return confirm('Delete this document?')\">"
             "<input type=\"hidden\" name=\"name\" value=\"");
  sb_put_esc(s, name, strlen(name));
  sb_puts(s, "\"><button class=\"btn danger\" type=\"submit\">delete</button></form></li>");
  return 0;
}

static void render_home(http_conn_t *c) {
  sb_t s; sb_init(&s);
  sb_puts(&s, PAGE_HEAD);
  sb_puts(&s, "<h1>Markdown Editor</h1>");

  sb_puts(&s, "<section class=\"card\"><h2>Documents</h2><ul class=\"docs\">");
  list_ctx_t lc = { &s, 0 };
  docstore_list(home_list_cb, &lc);
  if (lc.count == 0) sb_puts(&s, "<li class=\"empty\">No documents yet.</li>");
  sb_puts(&s, "</ul></section>");

  sb_puts(&s,
    "<section class=\"card\"><h2>New document</h2>"
    "<form method=\"get\" action=\"/edit\">"
    "<input type=\"text\" name=\"name\" placeholder=\"notes.md\" required>"
    "<button class=\"btn primary\" type=\"submit\">Create / open</button></form></section>");

  sb_puts(&s,
    "<section class=\"card\"><h2>Upload</h2>"
    "<form method=\"post\" action=\"/upload\" enctype=\"multipart/form-data\">"
    "<input type=\"file\" name=\"file\" accept=\".md,.markdown,.txt,text/*\" required>"
    "<button class=\"btn primary\" type=\"submit\">Upload</button></form></section>");

  sb_puts(&s, PAGE_FOOT);
  respond_sb(c, "200 OK", "text/html; charset=utf-8", &s);
}

static void render_edit(http_conn_t *c, const char *name) {
  char safe[DOCSTORE_MAX_NAME + 1];
  if (docstore_sanitize(safe, name) != 0) { respond_text(c, "400 Bad Request", "Bad name"); return; }

  /* Load existing content (if any) up to the inline edit cap. */
  char *content = NULL; uint32_t clen = 0; int too_big = 0;
  lfs_file_t f;
  if (docstore_open_read(&f, safe) == 0) {
    lfs_soff_t sz = docstore_size_of_open(&f);
    if (sz > (lfs_soff_t) EDIT_INLINE_MAX) {
      too_big = 1;
    } else if (sz > 0) {
      content = (char *) malloc((size_t) sz);
      if (content) {
        lfs_ssize_t r = docstore_read(&f, content, (uint32_t) sz);
        clen = (r > 0) ? (uint32_t) r : 0;
      }
    }
    docstore_close(&f);
  }

  sb_t s; sb_init(&s);
  sb_puts(&s, PAGE_HEAD);
  sb_puts(&s, "<div class=\"topbar\"><a class=\"btn\" href=\"/\">&larr; All docs</a><h1 class=\"fname\">");
  sb_put_esc(&s, safe, strlen(safe));
  sb_puts(&s, "</h1><a class=\"btn\" href=\"/doc/");
  sb_put_esc(&s, safe, strlen(safe));
  sb_puts(&s, "\">Download</a></div>");

  if (too_big) {
    sb_puts(&s, "<p class=\"warn\">This document is larger than the inline editor limit. "
                "Please download it to edit.</p>");
  } else {
    sb_puts(&s, "<form class=\"editor\" method=\"post\" action=\"/save\">"
                "<input type=\"hidden\" name=\"name\" value=\"");
    sb_put_esc(&s, safe, strlen(safe));
    sb_puts(&s, "\"><div class=\"panes\"><textarea id=\"src\" name=\"body\" spellcheck=\"false\">");
    if (content && clen) sb_put_esc(&s, content, clen);
    sb_puts(&s, "</textarea><div id=\"preview\" class=\"preview\"></div></div>"
                "<div class=\"actions\"><button class=\"btn primary\" type=\"submit\">Save</button>"
                "<span id=\"status\" class=\"muted\"></span></div></form>"
                "<script src=\"/md.js\"></script><script src=\"/app.js\"></script>");
  }
  sb_puts(&s, PAGE_FOOT);
  if (content) free(content);
  respond_sb(c, "200 OK", "text/html; charset=utf-8", &s);
}

static int api_list_cb(const char *name, uint32_t size, void *arg) {
  list_ctx_t *lc = (list_ctx_t *) arg;
  sb_t *s = lc->sb;
  if (lc->count++) sb_puts(s, ",");
  sb_puts(s, "{\"name\":\"");
  sb_put_esc(s, name, strlen(name));   /* names are sanitised; no quotes/backslashes */
  sb_puts(s, "\",\"size\":");
  char num[16]; snprintf(num, sizeof(num), "%lu", (unsigned long) size);
  sb_puts(s, num);
  sb_puts(s, "}");
  return 0;
}

static void render_api_list(http_conn_t *c) {
  sb_t s; sb_init(&s);
  sb_puts(&s, "[");
  list_ctx_t lc = { &s, 0 };
  docstore_list(api_list_cb, &lc);
  sb_puts(&s, "]");
  respond_sb(c, "200 OK", "application/json", &s);
}

/* Stream a document as a download. */
static void serve_download(http_conn_t *c, const char *name) {
  char safe[DOCSTORE_MAX_NAME + 1];
  if (docstore_sanitize(safe, name) != 0) { respond_text(c, "400 Bad Request", "Bad name"); return; }
  if (docstore_open_read(&c->file, safe) != 0) { respond_text(c, "404 Not Found", "No such document"); return; }
  c->file_open = 1;
  lfs_soff_t sz = docstore_size_of_open(&c->file);
  if (sz < 0) sz = 0;
  c->file_remaining = (uint32_t) sz;

  char extra[128];
  snprintf(extra, sizeof(extra), "Content-Disposition: attachment; filename=\"%s\"\r\n", safe);
  set_header(c, "200 OK", "text/markdown; charset=utf-8", (uint32_t) sz, extra);
  c->src = SRC_FILE;
}

/* ---- POST handlers ------------------------------------------------------ */

static void handle_save(http_conn_t *c) {
  if (!c->body) { respond_text(c, "400 Bad Request", "Empty body"); return; }
  char name[DOCSTORE_MAX_NAME + 1];
  if (form_get((const char *) c->body, c->body_len, "name", name, sizeof(name)) != 0) {
    respond_text(c, "400 Bad Request", "Missing name"); return;
  }
  /* The body field can be large; decode it in place into a fresh buffer. */
  char *bodyval = (char *) malloc(c->body_len + 1);
  if (!bodyval) { respond_text(c, "500 Internal Server Error", "OOM"); return; }
  if (form_get((const char *) c->body, c->body_len, "body", bodyval, c->body_len + 1) != 0) {
    bodyval[0] = '\0';   /* allow saving an empty document */
  }
  int rc = docstore_write(name, bodyval, (uint32_t) strlen(bodyval));
  free(bodyval);
  if (rc != 0) { respond_text(c, "500 Internal Server Error", "Write failed"); return; }

  char loc[128];
  /* Re-open the editor on the saved file. name is already sanitised-safe chars. */
  snprintf(loc, sizeof(loc), "/edit?name=%s", name);
  respond_redirect(c, loc);
}

static void handle_delete(http_conn_t *c) {
  if (!c->body) { respond_text(c, "400 Bad Request", "Empty body"); return; }
  char name[DOCSTORE_MAX_NAME + 1];
  if (form_get((const char *) c->body, c->body_len, "name", name, sizeof(name)) != 0) {
    respond_text(c, "400 Bad Request", "Missing name"); return;
  }
  docstore_delete(name);      /* ignore "not found" */
  respond_redirect(c, "/");
}

static void handle_upload(http_conn_t *c) {
  /* Extract boundary from Content-Type. */
  const char *bpos = strstr(c->content_type, "boundary=");
  if (!bpos || !c->body) { respond_text(c, "400 Bad Request", "Bad upload"); return; }
  bpos += strlen("boundary=");
  char boundary[80];
  size_t bi = 0;
  /* boundary may be quoted */
  if (*bpos == '"') { bpos++; while (*bpos && *bpos != '"' && bi + 1 < sizeof(boundary)) boundary[bi++] = *bpos++; }
  else { while (*bpos && *bpos != ';' && *bpos != ' ' && bi + 1 < sizeof(boundary)) boundary[bi++] = *bpos++; }
  boundary[bi] = '\0';
  if (bi == 0) { respond_text(c, "400 Bad Request", "No boundary"); return; }

  char delim[86];
  int dlen = snprintf(delim, sizeof(delim), "--%s", boundary);

  uint8_t *body = c->body;
  uint32_t blen = c->body_len;

  /* Find first part. */
  uint8_t *p = (uint8_t *) xmemmem(body, blen, delim, (size_t) dlen);
  char fname[DOCSTORE_MAX_NAME + 1]; fname[0] = '\0';
  const uint8_t *content = NULL; size_t content_len = 0;

  while (p) {
    uint8_t *after = p + dlen;
    size_t rem = (size_t) (body + blen - after);
    if (rem >= 2 && after[0] == '-' && after[1] == '-') break;   /* closing boundary */
    if (rem < 2) break;
    after += 2;                                                  /* skip CRLF */
    rem = (size_t) (body + blen - after);

    /* part headers end at CRLFCRLF */
    uint8_t *hend = (uint8_t *) xmemmem(after, rem, "\r\n\r\n", 4);
    if (!hend) break;
    size_t hlen = (size_t) (hend - after);

    /* look for filename="..." in this part's headers */
    char *fn = (char *) xmemmem(after, hlen, "filename=\"", 10);
    const uint8_t *cstart = hend + 4;

    /* content runs until the next "\r\n" + delim */
    char needle[90];
    int nlen = snprintf(needle, sizeof(needle), "\r\n%s", delim);
    size_t crem = (size_t) (body + blen - cstart);
    uint8_t *cend = (uint8_t *) xmemmem(cstart, crem, needle, (size_t) nlen);
    if (!cend) break;

    if (fn) {
      fn += 10;
      size_t fi = 0;
      while ((uint8_t *) fn < hend && *fn != '"' && fi + 1 < sizeof(fname)) fname[fi++] = *fn++;
      fname[fi] = '\0';
      content = cstart;
      content_len = (size_t) (cend - cstart);
      break;   /* first file part is enough */
    }
    /* not a file part; advance to next boundary */
    p = cend + 2;   /* skip the leading CRLF so p points at delim */
  }

  if (!content || fname[0] == '\0') { respond_text(c, "400 Bad Request", "No file part"); return; }
  if (content_len > DOCSTORE_MAX_DOC_SIZE) { respond_text(c, "413 Payload Too Large", "File too large"); return; }

  if (docstore_write(fname, content, (uint32_t) content_len) != 0) {
    respond_text(c, "500 Internal Server Error", "Write failed"); return;
  }
  respond_redirect(c, "/");
}

/* ---- routing ------------------------------------------------------------ */

static const web_asset_t *find_asset(const char *path) {
  for (unsigned i = 0; i < web_assets_count; i++) {
    if (strcmp(web_assets[i].path, path) == 0) return &web_assets[i];
  }
  return NULL;
}

static void dispatch(http_conn_t *c) {
  const char *path = c->path;

  if (c->method == M_GET) {
    if (strcmp(path, "/") == 0) { render_home(c); return; }
    if (strcmp(path, "/edit") == 0) {
      char name[DOCSTORE_MAX_NAME * 3 + 1];
      if (form_get(c->query, strlen(c->query), "name", name, sizeof(name)) != 0) {
        respond_text(c, "400 Bad Request", "Missing name"); return;
      }
      render_edit(c, name); return;
    }
    if (strcmp(path, "/api/list") == 0) { render_api_list(c); return; }
    if (ci_starts_with(path, "/doc/")) { serve_download(c, path + 5); return; }

    const web_asset_t *a = find_asset(path);
    if (a) { respond_mem(c, "200 OK", a->ctype, a->data, a->len, 0, NULL); return; }

    respond_text(c, "404 Not Found", "Not found");
    return;
  }

  if (c->method == M_POST) {
    if (strcmp(path, "/save") == 0)   { handle_save(c);   return; }
    if (strcmp(path, "/delete") == 0) { handle_delete(c); return; }
    if (strcmp(path, "/upload") == 0) { handle_upload(c); return; }
    respond_text(c, "404 Not Found", "Not found");
    return;
  }

  respond_text(c, "405 Method Not Allowed", "Method not allowed");
}

/* ---- request parsing ---------------------------------------------------- */

static void parse_headers(http_conn_t *c) {
  /* request line */
  char *line_end = strstr(c->hdr, "\r\n");
  if (!line_end) { c->method = M_OTHER; return; }

  if (ci_starts_with(c->hdr, "GET "))       c->method = M_GET;
  else if (ci_starts_with(c->hdr, "POST ")) c->method = M_POST;
  else                                      c->method = M_OTHER;

  const char *sp = strchr(c->hdr, ' ');
  const char *target = sp ? sp + 1 : c->hdr;
  const char *sp2 = strchr(target, ' ');
  size_t tlen = sp2 ? (size_t) (sp2 - target) : 0;

  /* split target into path?query */
  const char *q = memchr(target, '?', tlen);
  size_t plen = q ? (size_t) (q - target) : tlen;
  char rawpath[256];
  if (plen >= sizeof(rawpath)) plen = sizeof(rawpath) - 1;
  memcpy(rawpath, target, plen); rawpath[plen] = '\0';
  url_decode(c->path, sizeof(c->path), rawpath, plen);

  if (q) {
    size_t qlen = tlen - plen - 1;
    if (qlen >= sizeof(c->query)) qlen = sizeof(c->query) - 1;
    memcpy(c->query, q + 1, qlen); c->query[qlen] = '\0';
  } else {
    c->query[0] = '\0';
  }

  /* scan header lines for Content-Length / Content-Type */
  c->content_length = 0;
  c->content_type[0] = '\0';
  const char *ln = line_end + 2;
  while (ln && *ln) {
    const char *nl = strstr(ln, "\r\n");
    if (!nl || nl == ln) break;
    if (ci_starts_with(ln, "Content-Length:")) {
      c->content_length = strtol(ln + 15, NULL, 10);
    } else if (ci_starts_with(ln, "Content-Type:")) {
      const char *v = ln + 13;
      while (*v == ' ') v++;
      size_t vlen = (size_t) (nl - v);
      if (vlen >= sizeof(c->content_type)) vlen = sizeof(c->content_type) - 1;
      memcpy(c->content_type, v, vlen); c->content_type[vlen] = '\0';
    }
    ln = nl + 2;
  }
}

/* Feed a contiguous chunk of received bytes into the request state machine. */
static void feed(http_conn_t *c, const uint8_t *data, uint16_t len) {
  if (c->request_ready || c->responded) return;

  if (!c->headers_done) {
    int old = c->hdr_len;
    int space = HDR_CAP - 1 - c->hdr_len;
    int take = (len < (uint16_t) space) ? (int) len : space;
    memcpy(c->hdr + c->hdr_len, data, (size_t) take);
    c->hdr_len += take;
    c->hdr[c->hdr_len] = '\0';

    char *term = strstr(c->hdr, "\r\n\r\n");
    if (!term) {
      if (take < (int) len) {   /* header block exceeded HDR_CAP */
        respond_text(c, "431 Request Header Fields Too Large", "Headers too large");
        c->request_ready = 1;
      }
      return;
    }

    c->headers_done = 1;
    parse_headers(c);

    int header_bytes = (int) (term - c->hdr) + 4;
    int consumed_from_chunk = header_bytes - old;   /* chunk bytes that were header */
    const uint8_t *body_start = data + consumed_from_chunk;
    int body_in_chunk = (int) len - consumed_from_chunk;

    if (c->method == M_POST && c->content_length > 0) {
      if (c->content_length > (long) HTTP_MAX_BODY) {
        respond_text(c, "413 Payload Too Large", "Body too large");
        c->request_ready = 1;
        return;
      }
      c->body_cap = (uint32_t) c->content_length;
      c->body = (uint8_t *) malloc(c->body_cap + 1);
      if (!c->body) { respond_text(c, "500 Internal Server Error", "OOM"); c->request_ready = 1; return; }
      c->body_len = 0;
      if (body_in_chunk > 0) {
        uint32_t n = (uint32_t) body_in_chunk;
        if (n > c->body_cap) n = c->body_cap;
        memcpy(c->body, body_start, n);
        c->body_len = n;
      }
      if (c->body_len >= c->body_cap) { c->body[c->body_len] = '\0'; c->request_ready = 1; }
    } else {
      c->request_ready = 1;   /* GET, or POST with no body */
    }
    return;
  }

  /* body accumulation */
  if (c->body && c->body_len < c->body_cap) {
    uint32_t space = c->body_cap - c->body_len;
    uint32_t n = (len < space) ? len : space;
    memcpy(c->body + c->body_len, data, n);
    c->body_len += n;
    if (c->body_len >= c->body_cap) { c->body[c->body_len] = '\0'; c->request_ready = 1; }
  }
}

/* ---- response pump ------------------------------------------------------ */

static void response_finished(http_conn_t *c) {
  conn_free_response(c);
  c->closing = 1;
}

static void http_pump(http_conn_t *c) {
  if (c->src == SRC_NONE && c->rhdr_off >= c->rhdr_len) return;
  err_t err;

  /* headers */
  while (c->rhdr_off < c->rhdr_len) {
    u16_t space = tcp_sndbuf(c->pcb);
    if (space == 0) { tcp_output(c->pcb); return; }
    u16_t chunk = (u16_t) (c->rhdr_len - c->rhdr_off);
    if (chunk > space) chunk = space;
    err = tcp_write(c->pcb, c->rhdr + c->rhdr_off, chunk, TCP_WRITE_FLAG_COPY);
    if (err == ERR_MEM) { tcp_output(c->pcb); return; }
    if (err != ERR_OK) { conn_close(c); return; }
    c->rhdr_off += chunk;
  }

  if (c->src == SRC_MEM) {
    while (c->mem_off < c->mem_len) {
      u16_t space = tcp_sndbuf(c->pcb);
      if (space == 0) { tcp_output(c->pcb); return; }
      u16_t chunk = (c->mem_len - c->mem_off > space) ? space : (u16_t) (c->mem_len - c->mem_off);
      err = tcp_write(c->pcb, c->mem + c->mem_off, chunk, TCP_WRITE_FLAG_COPY);
      if (err == ERR_MEM) { tcp_output(c->pcb); return; }
      if (err != ERR_OK) { conn_close(c); return; }
      c->mem_off += chunk;
    }
    tcp_output(c->pcb);
    if (c->mem_off >= c->mem_len) response_finished(c);
  } else if (c->src == SRC_FILE) {
    uint8_t buf[FILE_CHUNK];
    while (c->file_remaining > 0) {
      u16_t space = tcp_sndbuf(c->pcb);
      if (space < 64) { tcp_output(c->pcb); return; }
      uint32_t want = c->file_remaining < FILE_CHUNK ? c->file_remaining : FILE_CHUNK;
      if (want > space) want = space;
      lfs_ssize_t r = docstore_read(&c->file, buf, want);
      if (r <= 0) { c->file_remaining = 0; break; }
      err = tcp_write(c->pcb, buf, (u16_t) r, TCP_WRITE_FLAG_COPY);
      if (err != ERR_OK) {
        /* couldn't queue; rewind the file so we re-read next time */
        docstore_close(&c->file);   /* simplest: on hard error, abort */
        c->file_open = 0; c->file_remaining = 0;
        conn_close(c);
        return;
      }
      c->file_remaining -= (uint32_t) r;
    }
    tcp_output(c->pcb);
    if (c->file_remaining == 0) response_finished(c);
  } else {
    /* header-only response (e.g. redirect) */
    response_finished(c);
  }

  if (c->closing) conn_close(c);
}

/* ---- lwIP callbacks ----------------------------------------------------- */

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
  (void) pcb; (void) len;
  http_conn_t *c = (http_conn_t *) arg;
  if (!c) return ERR_OK;
  http_pump(c);
  return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
  http_conn_t *c = (http_conn_t *) arg;
  if (!c) { if (p) pbuf_free(p); return ERR_OK; }

  if (p == NULL) { conn_close(c); return ERR_OK; }        /* peer closed */
  if (err != ERR_OK) { pbuf_free(p); conn_close(c); return ERR_OK; }

  tcp_recved(pcb, p->tot_len);
  for (struct pbuf *q = p; q != NULL; q = q->next) {
    feed(c, (const uint8_t *) q->payload, q->len);
  }
  pbuf_free(p);

  if (c->request_ready && !c->responded) {
    c->responded = 1;
    dispatch(c);
    http_pump(c);
  }
  return ERR_OK;
}

static void on_err(void *arg, err_t err) {
  (void) err;
  http_conn_t *c = (http_conn_t *) arg;
  if (!c) return;
  /* pcb already freed by lwIP; just release our state. */
  conn_free_response(c);
  if (c->body) free(c->body);
  free(c);
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
  http_conn_t *c = (http_conn_t *) arg;
  if (!c) { tcp_abort(pcb); return ERR_ABRT; }
  if (c->responded) http_pump(c);   /* nudge a stalled transfer */
  return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
  (void) arg;
  if (err != ERR_OK || newpcb == NULL) return ERR_VAL;

  http_conn_t *c = (http_conn_t *) calloc(1, sizeof(http_conn_t));
  if (!c) return ERR_MEM;
  c->pcb = newpcb;

  tcp_arg(newpcb, c);
  tcp_recv(newpcb, on_recv);
  tcp_sent(newpcb, on_sent);
  tcp_err(newpcb, on_err);
  tcp_poll(newpcb, on_poll, 8);     /* ~4s */
  tcp_nagle_disable(newpcb);
  return ERR_OK;
}

void http_server_init(void) {
  struct tcp_pcb *pcb = tcp_new();
  if (!pcb) return;
  if (tcp_bind(pcb, IP_ADDR_ANY, HTTP_PORT) != ERR_OK) return;
  struct tcp_pcb *lpcb = tcp_listen_with_backlog(pcb, 2);
  if (!lpcb) return;
  tcp_accept(lpcb, on_accept);
}
