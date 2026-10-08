#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <limits.h>
#include <execinfo.h>
#include <time.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "ur/ur.h"

static void
crash_handler(int sig)
{
  void *frames[64];
  int n = backtrace(frames, 64);
  char buf[64];
  int len = snprintf(buf, sizeof(buf), "\n--- CRASH: signal %d ---\n", sig);
  write(STDERR_FILENO, buf, len);
  backtrace_symbols_fd(frames, n, STDERR_FILENO);
  _exit(1);
}

// --- Noun helpers ---

static ur_nref
make_cord(ur_root_t *r, const char *s)
{
  return ur_coin_bytes(r, strlen(s), (uint8_t*)s);
}

static ur_nref
make_bytes(ur_root_t *r, const uint8_t *b, size_t len)
{
  return ur_coin_bytes(r, len, (uint8_t*)b);
}

static char*
read_cord(ur_root_t *r, ur_nref ref)
{
  if ( ur_nref_tag(ref) == ur_direct ) {
    uint64_t val = ur_nref_idx(ref);
    int len = 0;
    uint64_t tmp = val;
    while ( tmp ) { len++; tmp >>= 8; }
    char *s = malloc(len + 1);
    for ( int i = 0; i < len; i++ ) {
      s[i] = (val >> (8 * i)) & 0xff;
    }
    s[len] = '\0';
    return s;
  }
  else if ( ur_nref_tag(ref) == ur_iatom ) {
    uint64_t idx = ur_nref_idx(ref);
    uint64_t len = r->atoms.lens[idx];
    uint8_t *byt = r->atoms.bytes[idx];
    char *s = malloc(len + 1);
    memcpy(s, byt, len);
    s[len] = '\0';
    return s;
  }
  return strdup("???");
}

// Read raw bytes from an atom (caller frees, sets *out_len)
static uint8_t*
read_bytes(ur_root_t *r, ur_nref ref, size_t *out_len)
{
  if ( ur_nref_tag(ref) == ur_direct ) {
    uint64_t val = ur_nref_idx(ref);
    int len = 0;
    uint64_t tmp = val;
    while ( tmp ) { len++; tmp >>= 8; }
    uint8_t *b = malloc(len);
    for ( int i = 0; i < len; i++ ) {
      b[i] = (val >> (8 * i)) & 0xff;
    }
    *out_len = len;
    return b;
  }
  else if ( ur_nref_tag(ref) == ur_iatom ) {
    uint64_t idx = ur_nref_idx(ref);
    uint64_t len = r->atoms.lens[idx];
    uint8_t *byt = r->atoms.bytes[idx];
    uint8_t *b = malloc(len);
    memcpy(b, byt, len);
    *out_len = len;
    return b;
  }
  *out_len = 0;
  return NULL;
}

// Read an atom of at most 64 bits. Returns -1 for a cell or a wider atom.
static int
read_atom(ur_root_t *r, ur_nref ref, uint64_t *out)
{
  if ( ur_nref_tag(ref) == ur_direct ) {
    *out = ur_nref_idx(ref);
    return 0;
  }
  if ( ur_nref_tag(ref) == ur_iatom ) {
    uint64_t idx = ur_nref_idx(ref);
    uint64_t len = r->atoms.lens[idx];
    uint8_t *byt = r->atoms.bytes[idx];
    if ( len > 8 ) return -1;
    uint64_t val = 0;
    for ( uint64_t i = 0; i < len; i++ ) {
      val |= (uint64_t)byt[i] << (8 * i);
    }
    *out = val;
    return 0;
  }
  return -1;
}

static int
read_cell(ur_root_t *r, ur_nref ref, ur_nref *head, ur_nref *tail)
{
  if ( ur_nref_tag(ref) != ur_icell ) return -1;
  uint64_t ci = ur_nref_idx(ref);
  *head = r->cells.heads[ci];
  *tail = r->cells.tails[ci];
  return 0;
}

// Longest wire the sidecar tracks, as a string with its NUL, and the
// most segments. conn_t.wire and make_wire are sized to these.
#define WIRE_MAX  256
#define WIRE_SEGS 64

// Read a wire (path) from a noun — returns malloc'd string like "/foo/bar",
// or NULL if the wire is over WIRE_MAX or WIRE_SEGS
static char*
read_wire(ur_root_t *r, ur_nref ref)
{
  // A wire is a list of cords: [%foo %bar ~] -> "/foo/bar"
  char buf[WIRE_MAX] = {0};
  size_t pos = 0;
  int nseg = 0;
  ur_nref cur = ref;

  while ( ur_nref_tag(cur) == ur_icell ) {
    ur_nref h, t;
    read_cell(r, cur, &h, &t);
    char *seg = read_cord(r, h);
    size_t len = strlen(seg);
    if ( ++nseg > WIRE_SEGS || pos + 1 + len >= sizeof(buf) ) {
      free(seg);
      return NULL;
    }
    buf[pos++] = '/';
    memcpy(buf + pos, seg, len);
    pos += len;
    free(seg);
    cur = t;
  }

  if ( pos == 0 ) return strdup("/");
  return strdup(buf);
}

// Build a wire noun from a string like "/foo/bar"
static ur_nref
make_wire(ur_root_t *r, const char *path)
{
  // Parse "/foo/bar" into [%foo %bar ~]
  // We store segments then build the list in reverse
  const char *segs[WIRE_SEGS];
  int nseg = 0;
  const char *p = path;

  while ( *p ) {
    if ( *p == '/' ) { p++; continue; }
    segs[nseg++] = p;
    while ( *p && *p != '/' ) p++;
  }

  ur_nref list = 0;  // ~ (null)
  for ( int i = nseg - 1; i >= 0; i-- ) {
    const char *start = segs[i];
    const char *end = start;
    while ( *end && *end != '/' ) end++;
    size_t len = end - start;
    ur_nref cord = ur_coin_bytes(r, len, (uint8_t*)start);
    list = ur_cons(r, cord, list);
  }
  return list;
}

// --- Lick wire format ---

static uint8_t*
make_lick_msg(ur_root_t *r, ur_nref noun, uint32_t *out_len)
{
  uint64_t jam_len;
  uint8_t *jam_byt;
  ur_jam(r, noun, &jam_len, &jam_byt);

  uint32_t total = 1 + 4 + (uint32_t)jam_len;
  uint8_t *buf = malloc(total);

  buf[0] = 0;
  uint32_t jlen = (uint32_t)jam_len;
  buf[1] = (jlen >>  0) & 0xff;
  buf[2] = (jlen >>  8) & 0xff;
  buf[3] = (jlen >> 16) & 0xff;
  buf[4] = (jlen >> 24) & 0xff;
  memcpy(buf + 5, jam_byt, jam_len);

  free(jam_byt);
  *out_len = total;
  return buf;
}

static int
read_exact(int fd, uint8_t *buf, size_t n)
{
  size_t done = 0;
  while ( done < n ) {
    ssize_t r = read(fd, buf + done, n - done);
    if ( r < 0 && errno == EINTR ) continue;
    if ( r <= 0 ) return -1;
    done += r;
  }
  return 0;
}

static int
write_exact(int fd, const uint8_t *buf, size_t n)
{
  size_t done = 0;
  while ( done < n ) {
    ssize_t w = write(fd, buf + done, n - done);
    if ( w < 0 && errno == EINTR ) continue;
    if ( w <= 0 ) return -1;
    done += w;
  }
  return 0;
}

// --- Send gift nouns over Lick ---

// Send a gift on a wire given as a noun
static void
send_gift_on(ur_root_t *r, int lick_fd, const char *tag, ur_nref n_wire,
             ur_nref extra)
{
  ur_nref n_mark = make_cord(r, "tcp-gift");
  ur_nref n_tag  = make_cord(r, tag);
  ur_nref payload;

  if ( extra == (ur_nref)-1 ) {
    payload = ur_cons(r, n_tag, n_wire);
  } else {
    ur_nref inner = ur_cons(r, n_wire, extra);
    payload = ur_cons(r, n_tag, inner);
  }

  ur_nref msg_noun = ur_cons(r, n_mark, payload);

  uint32_t msg_len;
  uint8_t *msg = make_lick_msg(r, msg_noun, &msg_len);
  // A failed write means vere is gone. The poll loop sees the closed
  // socket on its next pass and reconnects; this gift has no reader.
  write_exact(lick_fd, msg, msg_len);
  free(msg);
}

static void
send_gift(ur_root_t *r, int lick_fd, const char *tag, const char *wire,
          ur_nref extra)
{
  send_gift_on(r, lick_fd, tag, make_wire(r, wire), extra);
}

static void
send_connected(ur_root_t *r, int lick_fd, const char *wire)
{
  send_gift(r, lick_fd, "connected", wire, (ur_nref)-1);
}

static void
send_receive(ur_root_t *r, int lick_fd, const char *wire,
             const uint8_t *data, size_t len)
{
  ur_nref n_len  = ur_coin64(r, (uint64_t)len);
  ur_nref n_data = make_bytes(r, data, len);
  ur_nref n_octs = ur_cons(r, n_len, n_data);
  send_gift(r, lick_fd, "receive", wire, n_octs);
}

static void
send_closed(ur_root_t *r, int lick_fd, const char *wire)
{
  send_gift(r, lick_fd, "closed", wire, (ur_nref)-1);
}

static void
send_error(ur_root_t *r, int lick_fd, const char *wire, const char *msg)
{
  printf("[%s] gift %%error: %s\n", wire, msg);
  ur_nref n_msg = make_cord(r, msg);
  send_gift(r, lick_fd, "error", wire, n_msg);
}

// Read a task's wire. A wire too long to track gets %error, sent on the
// wire as the task gave it, and NULL comes back.
static char*
task_wire(ur_root_t *r, int lick_fd, ur_nref wire_ref)
{
  char *wire = read_wire(r, wire_ref);
  if ( !wire ) {
    printf("task on a wire too long to track -> gift %%error\n");
    send_gift_on(r, lick_fd, "error", wire_ref, make_cord(r, "wire too long"));
  }
  return wire;
}

// --- Connection state ---

static uint64_t
now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

typedef enum {
  CONN_CONNECTING,
  CONN_HANDSHAKING,
  CONN_ACTIVE
} conn_state_t;

typedef struct {
  int          active;
  conn_state_t state;
  int          sock;
  SSL         *ssl;
  char         wire[WIRE_MAX];
  int          secure;
  char         host[256];
  uint64_t     deadline_ms;
  uint8_t     *out;            // Output the socket has not taken yet
  size_t       out_off;        // Start of the unsent bytes in out
  size_t       out_len;        // End of the unsent bytes in out
  size_t       out_cap;
  int          out_wants_in;   // TLS must read before it can write more
  int          closing;        // %close taken; flushing out, then closing
  int          in_more;        // Last read filled the buffer; read again
} conn_t;

static conn_t   *conns;
static size_t    conns_cap;
static size_t    conns_len;
static SSL_CTX  *ssl_ctx;

static conn_t*
find_conn(const char *wire)
{
  for ( size_t i = 0; i < conns_len; i++ ) {
    if ( conns[i].active && strcmp(conns[i].wire, wire) == 0 ) {
      return &conns[i];
    }
  }
  return NULL;
}

static conn_t*
alloc_conn(void)
{
  for ( size_t i = 0; i < conns_len; i++ ) {
    if ( !conns[i].active ) return &conns[i];
  }
  // No free slot — grow
  if ( conns_len == conns_cap ) {
    conns_cap = conns_cap ? conns_cap * 2 : 16;
    conns = realloc(conns, conns_cap * sizeof(conn_t));
  }
  conn_t *c = &conns[conns_len++];
  memset(c, 0, sizeof(*c));
  return c;
}

static void
close_conn(conn_t *c)
{
  if ( c->ssl ) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = NULL; }
  if ( c->sock >= 0 ) { close(c->sock); c->sock = -1; }
  free(c->out);
  c->out = NULL;
  c->out_off = c->out_len = c->out_cap = 0;
  c->in_more = 0;
  c->active = 0;
}

// --- Output buffer ---

// Largest %send, and the most unsent output one connection may hold
#define SEND_MAX (64u << 20)

// How long a closed connection may take to flush its output
#define DRAIN_MS 30000

static size_t
conn_pending(conn_t *c)
{
  return c->out_len - c->out_off;
}

// Append data to a connection's output. Returns -1 if it would exceed
// SEND_MAX.
static int
conn_queue(conn_t *c, const uint8_t *data, size_t len)
{
  size_t have = conn_pending(c);
  if ( len > SEND_MAX - have ) return -1;

  if ( c->out_off ) {
    memmove(c->out, c->out + c->out_off, have);
    c->out_off = 0;
    c->out_len = have;
  }
  if ( have + len > c->out_cap ) {
    c->out_cap = have + len;
    c->out = realloc(c->out, c->out_cap);
  }
  memcpy(c->out + have, data, len);
  c->out_len = have + len;
  return 0;
}

// Write as much output as the socket takes; the rest stays buffered.
// Returns -1 if the connection failed.
static int
conn_flush(conn_t *c)
{
  c->out_wants_in = 0;

  while ( conn_pending(c) ) {
    size_t  left = conn_pending(c);
    ssize_t n;

    if ( c->ssl ) {
      n = SSL_write(c->ssl, c->out + c->out_off,
                    left > INT_MAX ? INT_MAX : (int)left);
      if ( n <= 0 ) {
        int err = SSL_get_error(c->ssl, n);
        if ( err == SSL_ERROR_WANT_WRITE ) return 0;
        if ( err == SSL_ERROR_WANT_READ ) { c->out_wants_in = 1; return 0; }
        return -1;
      }
    } else {
      n = write(c->sock, c->out + c->out_off, left);
      if ( n < 0 ) {
        if ( errno == EINTR ) continue;
        if ( errno == EAGAIN || errno == EWOULDBLOCK ) return 0;
        return -1;
      }
    }
    c->out_off += n;
  }

  free(c->out);
  c->out = NULL;
  c->out_off = c->out_len = c->out_cap = 0;
  return 0;
}

// --- Input ---

// Most bytes one %receive gift carries. Every gift is an Arvo event, so
// a small buffer turns one large message into hundreds of events.
#define RECV_MAX (1u << 20)

// Static: the poll loop is single-threaded
static uint8_t recv_buf[RECV_MAX];

// Read into recv_buf until the socket is empty or the buffer is full.
// Returns the byte count. Closes the connection on EOF or error; the
// wire stays in c->wire for the caller's gift.
static size_t
conn_read(conn_t *c)
{
  size_t got = 0;
  int    eof = 0;

  while ( got < RECV_MAX ) {
    ssize_t n;

    if ( c->ssl ) {
      n = SSL_read(c->ssl, recv_buf + got, (int)(RECV_MAX - got));
      if ( n <= 0 ) {
        int err = SSL_get_error(c->ssl, n);
        eof = ( err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE );
        break;
      }
    } else {
      n = read(c->sock, recv_buf + got, RECV_MAX - got);
      if ( n < 0 && errno == EINTR ) continue;
      if ( n <= 0 ) {
        eof = ( n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK) );
        break;
      }
    }
    got += n;
  }

  // A full buffer may leave bytes behind, and TLS can hold them where
  // poll does not see them: come straight back
  c->in_more = ( got == RECV_MAX );

  if ( eof ) close_conn(c);
  return got;
}

// What to poll an active connection for
static short
c_events(conn_t *c)
{
  short ev = c->closing ? 0 : POLLIN;
  if ( conn_pending(c) ) ev |= c->out_wants_in ? POLLIN : POLLOUT;
  return ev;
}

// --- Set fd non-blocking ---

static void
set_nonblock(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// --- Handle commands from the agent ---

// Longest connect timeout, in milliseconds
#define TIMEOUT_MAX 0xffffffffULL

// Read a port. Returns -1 unless it is an atom from 1 to 65535.
static int
read_port(ur_root_t *r, ur_nref ref, uint64_t *port)
{
  if ( read_atom(r, ref, port) < 0 ) return -1;
  if ( *port == 0 || *port > 65535 ) return -1;
  return 0;
}

// Parse a target [secure=? [%tag addr port]] into a host and a port.
// Returns NULL, or the message for the %error gift.
static const char*
read_target(ur_root_t *r, ur_nref target_ref, int *secure,
            char *host, size_t host_len, uint64_t *port)
{
  ur_nref secure_ref, fief_ref;
  if ( read_cell(r, target_ref, &secure_ref, &fief_ref) < 0 ) {
    return "bad target";
  }

  uint64_t loob;
  if ( read_atom(r, secure_ref, &loob) < 0 || loob > 1 ) return "bad target";
  *secure = (loob == 0) ? 1 : 0;  // 0=yes, 1=no in loobean

  // Parse fief: [%tag p q]
  ur_nref tag_ref, rest;
  if ( read_cell(r, fief_ref, &tag_ref, &rest) < 0 ) return "bad fief";

  ur_nref addr_ref, port_ref;
  if ( read_cell(r, rest, &addr_ref, &port_ref) < 0 ) return "bad fief";

  char *tag = read_cord(r, tag_ref);
  const char *err = NULL;

  if ( strcmp(tag, "turf") == 0 ) {
    // Use the first turf: [%com %example ~] -> "example.com"
    ur_nref turf_ref, t;
    if ( read_cell(r, addr_ref, &turf_ref, &t) < 0 ) {
      err = "empty turf list";
    }
    else {
      char *segs[32];
      int nseg = 0;
      size_t need = 0;
      ur_nref cur = turf_ref;
      while ( ur_nref_tag(cur) == ur_icell && nseg < 32 ) {
        ur_nref h;
        read_cell(r, cur, &h, &cur);
        segs[nseg] = read_cord(r, h);
        need += strlen(segs[nseg]) + 1;
        nseg++;
      }

      if ( nseg == 0 || ur_nref_tag(cur) == ur_icell || need > host_len ) {
        err = "bad address";
      }
      else {
        size_t pos = 0;
        for ( int i = nseg - 1; i >= 0; i-- ) {
          if ( pos > 0 ) host[pos++] = '.';
          size_t slen = strlen(segs[i]);
          memcpy(host + pos, segs[i], slen);
          pos += slen;
        }
        host[pos] = '\0';
      }
      for ( int i = 0; i < nseg; i++ ) free(segs[i]);
    }
  }
  else if ( strcmp(tag, "if") == 0 ) {
    uint64_t ip;
    if ( read_atom(r, addr_ref, &ip) < 0 || ip > 0xffffffffULL ) {
      err = "bad address";
    }
    else {
      snprintf(host, host_len, "%llu.%llu.%llu.%llu",
        (unsigned long long)((ip >> 24) & 0xff),
        (unsigned long long)((ip >> 16) & 0xff),
        (unsigned long long)((ip >> 8) & 0xff),
        (unsigned long long)(ip & 0xff));
    }
  }
  else if ( strcmp(tag, "is") == 0 ) {
    size_t raw_len = 0;
    uint8_t *raw = read_bytes(r, addr_ref, &raw_len);
    if ( ur_nref_tag(addr_ref) == ur_icell || raw_len > 16 ) {
      err = "bad address";
    }
    else {
      uint8_t ip6[16] = {0};
      if ( raw_len ) memcpy(ip6, raw, raw_len);
      snprintf(host, host_len,
        "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
        "%02x%02x:%02x%02x:%02x%02x:%02x%02x",
        ip6[15], ip6[14], ip6[13], ip6[12],
        ip6[11], ip6[10], ip6[9],  ip6[8],
        ip6[7],  ip6[6],  ip6[5],  ip6[4],
        ip6[3],  ip6[2],  ip6[1],  ip6[0]);
    }
    free(raw);
  }
  else {
    err = "unsupported fief type";
  }
  free(tag);

  if ( !err && read_port(r, port_ref, port) < 0 ) err = "bad port";
  return err;
}

static void
handle_connect(ur_root_t *r, int lick_fd, ur_nref wire_ref, ur_nref rest)
{
  char *wire = task_wire(r, lick_fd, wire_ref);
  if ( !wire ) return;
  printf("[%s] task %%connect\n", wire);

  conn_t *old = find_conn(wire);
  if ( old ) {
    // The agent drops a wire on %error, so drop the connection too
    send_error(r, lick_fd, wire, "connection already exists");
    close_conn(old);
    free(wire);
    return;
  }

  // Detect new format [wire [target timeout]] vs old [wire target]
  // Old: rest = [secure fief] — head is atom (loobean)
  // New: rest = [[secure fief] timeout] — head is cell
  ur_nref head, tail;
  ur_nref target_ref = rest;
  uint64_t timeout_ms = 30000;

  if ( read_cell(r, rest, &head, &tail) == 0
       && ur_nref_tag(head) == ur_icell ) {
    target_ref = head;

    // timeout is a (unit @ud): ~ or [~ ms]
    ur_nref u_tag, u_val;
    if ( read_cell(r, tail, &u_tag, &u_val) == 0 ) {
      uint64_t v;
      if ( read_atom(r, u_val, &v) < 0 || v > TIMEOUT_MAX ) {
        send_error(r, lick_fd, wire, "bad timeout");
        free(wire);
        return;
      }
      if ( v > 0 ) timeout_ms = v;
    }
  }

  int secure = 0;
  char host[256] = {0};
  uint64_t port = 0;

  const char *bad = read_target(r, target_ref, &secure, host, sizeof(host),
                                &port);
  if ( bad ) {
    send_error(r, lick_fd, wire, bad);
    free(wire);
    return;
  }

  printf("[%s] resolving %s:%llu %s (timeout %llums)\n", wire, host,
    (unsigned long long)port, secure ? "(tls)" : "(plain)",
    (unsigned long long)timeout_ms);

  // DNS resolve (blocking — async DNS is platform-specific)
  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%llu", (unsigned long long)port);

  struct addrinfo hints = {0}, *res;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  int rc = getaddrinfo(host, port_str, &hints, &res);
  if ( rc != 0 ) {
    char err[256];
    snprintf(err, sizeof(err), "dns: %s", gai_strerror(rc));
    send_error(r, lick_fd, wire, err);
    free(wire);
    return;
  }

  // Non-blocking connect
  int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if ( sock < 0 ) {
    send_error(r, lick_fd, wire, strerror(errno));
    freeaddrinfo(res);
    free(wire);
    return;
  }
  set_nonblock(sock);

  int ret = connect(sock, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);

  conn_t *c = alloc_conn();
  memset(c, 0, sizeof(*c));
  c->active = 1;
  c->sock = sock;
  c->secure = secure;
  strncpy(c->wire, wire, sizeof(c->wire) - 1);
  strncpy(c->host, host, sizeof(c->host) - 1);
  c->deadline_ms = now_ms() + timeout_ms;

  if ( ret == 0 ) {
    // Instant connect (localhost etc)
    if ( secure ) {
      c->ssl = SSL_new(ssl_ctx);
      SSL_set_fd(c->ssl, c->sock);
      SSL_set_tlsext_host_name(c->ssl, c->host);
      SSL_set_connect_state(c->ssl);
      c->state = CONN_HANDSHAKING;
    } else {
      c->state = CONN_ACTIVE;
      send_connected(r, lick_fd, wire);
      printf("[%s] gift %%connected -> %s:%llu\n", wire, host, (unsigned long long)port);
    }
  }
  else if ( errno == EINPROGRESS ) {
    c->state = CONN_CONNECTING;
    printf("[%s] connecting async...\n", wire);
  }
  else {
    char err[256];
    snprintf(err, sizeof(err), "connect: %s", strerror(errno));
    send_error(r, lick_fd, wire, err);
    close(sock);
    c->active = 0;
  }

  free(wire);
}

static void
handle_send(ur_root_t *r, int lick_fd, ur_nref wire_ref, ur_nref octs_ref)
{
  char *wire = task_wire(r, lick_fd, wire_ref);
  if ( !wire ) return;

  conn_t *c = find_conn(wire);

  // octs: [p=@ud q=@]
  ur_nref len_ref, data_ref;
  uint64_t len;
  if ( read_cell(r, octs_ref, &len_ref, &data_ref) < 0
       || read_atom(r, len_ref, &len) < 0
       || len > SEND_MAX
       || ur_nref_tag(data_ref) == ur_icell ) {
    // The agent drops a wire on %error, so drop the connection too
    send_error(r, lick_fd, wire, "bad octs");
    if ( c ) close_conn(c);
    free(wire);
    return;
  }

  size_t raw_len;
  uint8_t *data = read_bytes(r, data_ref, &raw_len);

  // Use the octs length (preserves trailing nulls)
  if ( raw_len < len ) {
    data = realloc(data, len);
    memset(data + raw_len, 0, len - raw_len);
  }
  printf("[%s] task %%send %llu bytes\n", wire, (unsigned long long)len);

  if ( !c ) {
    send_error(r, lick_fd, wire, "no such connection");
    free(data);
    free(wire);
    return;
  }

  // Queue behind whatever is still unsent, so the bytes keep their order.
  // A connection still connecting holds its output until it is up.
  if ( conn_queue(c, data, (size_t)len) < 0 ) {
    send_error(r, lick_fd, wire, "send buffer full");
    close_conn(c);
  }
  else if ( c->state == CONN_ACTIVE && conn_flush(c) < 0 ) {
    send_error(r, lick_fd, wire, "send failed");
    close_conn(c);
  }
  else if ( conn_pending(c) ) {
    printf("[%s] %zu bytes buffered\n", wire, conn_pending(c));
  }

  free(data);
  free(wire);
}

static void
handle_close(ur_root_t *r, int lick_fd, ur_nref wire_ref)
{
  char *wire = task_wire(r, lick_fd, wire_ref);
  if ( !wire ) return;
  printf("[%s] task %%close\n", wire);
  conn_t *c = find_conn(wire);

  if ( c ) {
    if ( c->state == CONN_ACTIVE && conn_pending(c) ) {
      // Unsent output: keep the socket until it drains. The wire is free
      // at once: no real wire is empty, so nothing finds this connection.
      printf("[%s] closing after %zu buffered bytes\n", wire,
             conn_pending(c));
      c->closing = 1;
      c->in_more = 0;
      c->wire[0] = '\0';
      c->deadline_ms = now_ms() + DRAIN_MS;
    } else {
      close_conn(c);
    }
    send_closed(r, lick_fd, wire);
    printf("[%s] gift %%closed\n", wire);
  }

  free(wire);
}

// --- Lick connection ---

// Connect to vere's Lick socket. Tries once a second until vere is there.
static int
lick_connect(const struct sockaddr_un *addr)
{
  int waiting = 0;

  while ( 1 ) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if ( fd < 0 ) { perror("socket"); exit(1); }

    if ( connect(fd, (struct sockaddr*)addr, sizeof(*addr)) == 0 ) {
      printf("lick: connected\n");
      return fd;
    }
    if ( !waiting ) {
      printf("lick: connect: %s; trying again every second\n",
             strerror(errno));
      waiting = 1;
    }
    close(fd);
    sleep(1);
  }
}

// Vere went away. Close every connection, then wait for vere to return.
// No gifts go out: nothing is left to read them, and the agent closes
// its own wires when it hears %disconnect.
static int
lick_reconnect(int lick_fd, const struct sockaddr_un *addr)
{
  size_t n = 0;
  for ( size_t i = 0; i < conns_len; i++ ) {
    if ( conns[i].active ) { close_conn(&conns[i]); n++; }
  }
  printf("lick: connection closed; dropped %zu connections\n", n);
  close(lick_fd);
  return lick_connect(addr);
}

// --- Main loop ---

int
main(int argc, char **argv)
{
  if ( argc < 2 ) {
    fprintf(stderr, "usage: %s <pier-path>\n", argv[0]);
    return 1;
  }

  setbuf(stdout, NULL);
  signal(SIGSEGV, crash_handler);
  signal(SIGBUS, crash_handler);
  signal(SIGABRT, crash_handler);
  signal(SIGPIPE, SIG_IGN);

  // Init OpenSSL
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  ssl_ctx = SSL_CTX_new(TLS_client_method());
  SSL_CTX_set_default_verify_paths(ssl_ctx);
  SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
  // SSL_write may take part of a buffer, and conn_queue may move it
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE
                          | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

  conns = NULL;
  conns_cap = 0;
  conns_len = 0;

  // Connect to Lick socket
  char sock_path[4096];
  snprintf(sock_path, sizeof(sock_path), "%s/.urb/dev/tcp/tcp", argv[1]);

  struct sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  if ( strlen(sock_path) >= sizeof(addr.sun_path) ) {
    // A truncated path would never connect, and the retry would hide it
    fprintf(stderr, "socket path too long: %s\n"
                    "run from inside the pier: cd <pier-path> && %s .\n",
                    sock_path, argv[0]);
    return 1;
  }
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  printf("lick: connecting to %s\n", sock_path);
  int lick_fd = lick_connect(&addr);

  ur_root_t *r = ur_root_init();

  // Make lick_fd non-blocking for the poll loop
  // lick_fd stays blocking: poll() guards reads, writes must complete fully

  while ( 1 ) {
    // Check timeouts on pending connections
    uint64_t ts = now_ms();
    for ( size_t i = 0; i < conns_len; i++ ) {
      conn_t *c = &conns[i];
      if ( !c->active ) continue;
      if ( c->closing ) {
        if ( ts >= c->deadline_ms ) {
          printf("closed connection dropped %zu unsent bytes\n",
                 conn_pending(c));
          close_conn(c);
        }
        continue;
      }
      if ( c->state == CONN_ACTIVE ) continue;
      if ( ts >= c->deadline_ms ) {
        printf("[%s] timeout (%s)\n", c->wire,
          c->state == CONN_CONNECTING ? "connect" : "tls");
        char wire_copy[WIRE_MAX];
        strncpy(wire_copy, c->wire, sizeof(wire_copy));
        close_conn(c);
        send_error(r, lick_fd, wire_copy, "connect timeout");
        ur_root_free(r);
        r = ur_root_init();
      }
    }

    // Build poll set: lick_fd + all active connection sockets
    size_t pfd_cap = 1 + conns_len;
    struct pollfd *pfds = malloc(pfd_cap * sizeof(struct pollfd));
    size_t *conn_pfd_map = malloc(pfd_cap * sizeof(size_t));
    int nfds = 0;

    pfds[nfds].fd = lick_fd;
    pfds[nfds].events = POLLIN;
    nfds++;

    int wait_ms = 100;

    for ( size_t i = 0; i < conns_len; i++ ) {
      if ( !conns[i].active ) continue;
      conn_pfd_map[nfds - 1] = i;
      if ( conns[i].in_more ) wait_ms = 0;
      pfds[nfds].fd = conns[i].sock;
      switch ( conns[i].state ) {
        case CONN_CONNECTING:  pfds[nfds].events = POLLOUT; break;
        case CONN_HANDSHAKING: pfds[nfds].events = POLLIN | POLLOUT; break;
        case CONN_ACTIVE:
          // Unsent output waits for the socket to take more, or for TLS
          // to read first. A closing connection reads nothing else.
          pfds[nfds].events = c_events(&conns[i]);
          break;
      }
      nfds++;
    }

    int ready = poll(pfds, nfds, wait_ms);
    if ( ready < 0 ) {
      if ( errno == EINTR ) goto next;
      perror("poll");
      free(pfds);
      free(conn_pfd_map);
      break;
    }

    // Handle connection events
    for ( int p = 1; p < nfds; p++ ) {
      size_t ci = conn_pfd_map[p - 1];
      conn_t *c = &conns[ci];

      if ( !pfds[p].revents && !c->in_more ) continue;

      // TCP connect completing
      if ( c->state == CONN_CONNECTING ) {
        if ( pfds[p].revents & (POLLOUT | POLLERR | POLLHUP) ) {
          int err;
          socklen_t elen = sizeof(err);
          getsockopt(c->sock, SOL_SOCKET, SO_ERROR, &err, &elen);
          if ( err != 0 ) {
            char msg[256];
            snprintf(msg, sizeof(msg), "connect: %s", strerror(err));
            char wire_copy[WIRE_MAX];
            strncpy(wire_copy, c->wire, sizeof(wire_copy));
            close_conn(c);
            send_error(r, lick_fd, wire_copy, msg);
            ur_root_free(r);
            r = ur_root_init();
          }
          else if ( c->secure ) {
            c->ssl = SSL_new(ssl_ctx);
            SSL_set_fd(c->ssl, c->sock);
            SSL_set_tlsext_host_name(c->ssl, c->host);
            SSL_set_connect_state(c->ssl);
            c->state = CONN_HANDSHAKING;
            printf("[%s] tcp connected, starting tls\n", c->wire);
          }
          else {
            c->state = CONN_ACTIVE;
            send_connected(r, lick_fd, c->wire);
            printf("[%s] gift %%connected\n", c->wire);
            ur_root_free(r);
            r = ur_root_init();
          }
        }
        continue;
      }

      // TLS handshake in progress
      if ( c->state == CONN_HANDSHAKING ) {
        int ret = SSL_connect(c->ssl);
        if ( ret == 1 ) {
          c->state = CONN_ACTIVE;
          send_connected(r, lick_fd, c->wire);
          printf("[%s] tls handshake ok, gift %%connected\n", c->wire);
          ur_root_free(r);
          r = ur_root_init();
        } else {
          int err = SSL_get_error(c->ssl, ret);
          if ( err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE ) {
            char wire_copy[WIRE_MAX];
            strncpy(wire_copy, c->wire, sizeof(wire_copy));
            close_conn(c);
            send_error(r, lick_fd, wire_copy, "tls handshake failed");
            ur_root_free(r);
            r = ur_root_init();
          }
        }
        continue;
      }

      // Closed by the agent — flush, then drop the socket
      if ( c->closing ) {
        if ( conn_flush(c) < 0 || !conn_pending(c) ) close_conn(c);
        continue;
      }

      // Active connection — handle incoming data
      if ( c->in_more || (pfds[p].revents & (POLLIN | POLLHUP | POLLERR)) ) {
        size_t got = conn_read(c);

        if ( got ) {
          printf("[%s] gift %%receive %zu bytes\n", c->wire, got);
          send_receive(r, lick_fd, c->wire, recv_buf, got);
        }
        if ( !c->active ) {
          printf("[%s] remote closed -> gift %%closed\n", c->wire);
          send_closed(r, lick_fd, c->wire);
        }

        ur_root_free(r);
        r = ur_root_init();
      }

      // Still open — send buffered output. After the read, so a peer
      // that closed gets %closed, not a failed send.
      if ( c->active && conn_pending(c) && conn_flush(c) < 0 ) {
        char wire_copy[WIRE_MAX];
        strncpy(wire_copy, c->wire, sizeof(wire_copy));
        close_conn(c);
        send_error(r, lick_fd, wire_copy, "send failed");
        ur_root_free(r);
        r = ur_root_init();
      }
    }

    // Check for messages from Lick
    if ( pfds[0].revents & (POLLIN | POLLHUP | POLLERR) ) {
      uint8_t hdr[5];
      if ( read_exact(lick_fd, hdr, 5) < 0 ) {
        lick_fd = lick_reconnect(lick_fd, &addr);
        goto next;
      }

      uint32_t jam_len =
          ((uint32_t)hdr[4] << 24)
        | ((uint32_t)hdr[3] << 16)
        | ((uint32_t)hdr[2] <<  8)
        | ((uint32_t)hdr[1]);

      uint8_t *jam_byt = malloc(jam_len);
      if ( read_exact(lick_fd, jam_byt, jam_len) < 0 ) {
        free(jam_byt);
        lick_fd = lick_reconnect(lick_fd, &addr);
        goto next;
      }

      ur_nref noun;
      if ( ur_cue(r, jam_len, jam_byt, &noun) != ur_cue_good ) {
        printf("cue failed\n");
        free(jam_byt);
        goto next;
      }
      free(jam_byt);

      // Expect [%tcp-task payload]
      ur_nref mark_ref, payload;
      if ( read_cell(r, noun, &mark_ref, &payload) < 0 ) goto next;

      char *mark = read_cord(r, mark_ref);
      if ( strcmp(mark, "tcp-task") != 0 ) {
        printf("lick: ignoring mark %s\n", mark);
        free(mark);
        goto next;
      }
      free(mark);

      // payload is [%cmd wire ...]
      ur_nref cmd_ref, cmd_rest;
      if ( read_cell(r, payload, &cmd_ref, &cmd_rest) < 0 ) goto next;

      char *cmd = read_cord(r, cmd_ref);

      if ( strcmp(cmd, "connect") == 0 ) {
        ur_nref wire_ref, rest;
        if ( read_cell(r, cmd_rest, &wire_ref, &rest) == 0 ) {
          handle_connect(r, lick_fd, wire_ref, rest);
        }
      }
      else if ( strcmp(cmd, "send") == 0 ) {
        ur_nref wire_ref, data_ref;
        if ( read_cell(r, cmd_rest, &wire_ref, &data_ref) == 0 ) {
          handle_send(r, lick_fd, wire_ref, data_ref);
        }
      }
      else if ( strcmp(cmd, "close") == 0 ) {
        handle_close(r, lick_fd, cmd_rest);
      }
      else {
        printf("unknown command: %s\n", cmd);
      }

      free(cmd);

      ur_root_free(r);
      r = ur_root_init();
    }

  next:
    free(pfds);
    free(conn_pfd_map);
  }

  // Cleanup
  for ( size_t i = 0; i < conns_len; i++ ) {
    if ( conns[i].active ) close_conn(&conns[i]);
  }
  free(conns);

  ur_root_free(r);
  close(lick_fd);
  SSL_CTX_free(ssl_ctx);
  return 0;
}
