// Tests for the sidecar
//
// Plays both ends: vere's side of the Lick socket and the remote side of
// each TCP connection. Sends tasks, then checks the gifts and the bytes.
//
// Run with: make test

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ur/ur.h"

#define LICK_PATH ".urb/dev/tcp/tcp"
#define LOG_PATH  "sidecar.log"
#define WAIT_MS   5000

static char       pier[PATH_MAX];
static char       sidecar_bin[PATH_MAX];
static pid_t      sidecar = -1;
static int        lick_srv = -1;
static int        lick = -1;
static ur_root_t *r;
static int        checks;
static int        fails;

// --- Harness ---

static void
cleanup(void)
{
  if ( sidecar > 0 ) {
    kill(sidecar, SIGKILL);
    waitpid(sidecar, NULL, 0);
    sidecar = -1;
  }
  unlink(LICK_PATH);
  rmdir(".urb/dev/tcp");
  rmdir(".urb/dev");
  rmdir(".urb");
  if ( fails == 0 ) {
    unlink(LOG_PATH);
    rmdir(pier);
  }
}

static void
die(const char *what)
{
  fprintf(stderr, "test: %s: %s\n", what, strerror(errno));
  fails++;
  cleanup();
  exit(2);
}

static void
on_alarm(int sig)
{
  (void)sig;
  static const char msg[] = "test: timed out\n";
  write(STDERR_FILENO, msg, sizeof(msg) - 1);
  if ( sidecar > 0 ) kill(sidecar, SIGKILL);
  _exit(2);
}

static void
check(int ok, const char *what)
{
  checks++;
  if ( !ok ) fails++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
}

static void
write_all(int fd, const uint8_t *buf, size_t n)
{
  size_t done = 0;
  while ( done < n ) {
    ssize_t w = write(fd, buf + done, n - done);
    if ( w < 0 ) {
      if ( errno == EINTR ) continue;
      die("write");
    }
    done += w;
  }
}

// Read exactly n bytes, waiting at most ms for each piece.
// Returns the count read: less than n on timeout or EOF.
static size_t
read_n(int fd, uint8_t *buf, size_t n, int ms)
{
  size_t done = 0;
  while ( done < n ) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    if ( poll(&p, 1, ms) <= 0 ) break;
    ssize_t got = read(fd, buf + done, n - done);
    if ( got < 0 && errno == EINTR ) continue;
    if ( got <= 0 ) break;
    done += got;
  }
  return done;
}

// Returns 1 if the peer closed fd within ms, 0 otherwise
static int
saw_eof(int fd, int ms)
{
  uint8_t b;
  struct pollfd p = { .fd = fd, .events = POLLIN };
  if ( poll(&p, 1, ms) <= 0 ) return 0;
  return read(fd, &b, 1) <= 0;
}

// --- The Lick side ---

static void
lick_listen(void)
{
  mkdir(".urb", 0700);
  mkdir(".urb/dev", 0700);
  mkdir(".urb/dev/tcp", 0700);
  unlink(LICK_PATH);

  lick_srv = socket(AF_UNIX, SOCK_STREAM, 0);
  if ( lick_srv < 0 ) die("socket");

  struct sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, LICK_PATH, sizeof(addr.sun_path) - 1);
  if ( bind(lick_srv, (struct sockaddr*)&addr, sizeof(addr)) < 0 ) die("bind");
  if ( listen(lick_srv, 1) < 0 ) die("listen");
}

// Returns 0 once the sidecar connects, -1 if it does not within ms
static int
lick_accept(int ms)
{
  struct pollfd p = { .fd = lick_srv, .events = POLLIN };
  if ( poll(&p, 1, ms) <= 0 ) return -1;
  lick = accept(lick_srv, NULL, NULL);
  return lick < 0 ? -1 : 0;
}

static void
sidecar_start(void)
{
  sidecar = fork();
  if ( sidecar < 0 ) die("fork");
  if ( sidecar == 0 ) {
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    execl(sidecar_bin, "tcp-sidecar", ".", (char*)NULL);
    _exit(127);
  }
}

// Returns 1 if the sidecar process is still running
static int
sidecar_alive(void)
{
  return sidecar > 0 && waitpid(sidecar, NULL, WNOHANG) == 0;
}

// --- Nouns ---

static ur_nref
cord(const char *s)
{
  return ur_coin_bytes(r, strlen(s), (uint8_t*)s);
}

// A one-segment wire: "foo" -> /foo
static ur_nref
wire(const char *s)
{
  return ur_cons(r, cord(s), 0);
}

// An atom of len bytes, every byte 0xff
static ur_nref
wide(size_t len)
{
  uint8_t byt[32];
  memset(byt, 0xff, sizeof(byt));
  return ur_coin_bytes(r, len, byt);
}

static ur_nref
unit(ur_nref n)
{
  return ur_cons(r, 0, n);
}

static ur_nref
fief(const char *tag, ur_nref addr, ur_nref port)
{
  return ur_cons(r, cord(tag), ur_cons(r, addr, port));
}

// Send a task to the sidecar: [%tcp-task task]
static void
put(ur_nref task)
{
  ur_nref noun = ur_cons(r, cord("tcp-task"), task);
  uint64_t jam_len;
  uint8_t *jam_byt;
  ur_jam(r, noun, &jam_len, &jam_byt);

  uint8_t hdr[5] = { 0, jam_len & 0xff, (jam_len >> 8) & 0xff,
                     (jam_len >> 16) & 0xff, (jam_len >> 24) & 0xff };
  write_all(lick, hdr, 5);
  write_all(lick, jam_byt, jam_len);
  free(jam_byt);

  ur_root_free(r);
  r = ur_root_init();
}

// [%connect wire [secure fief] timeout=(unit @ud)], plain tcp
static void
put_connect(const char *w, ur_nref fief_ref, ur_nref timeout)
{
  ur_nref target = ur_cons(r, 1, fief_ref);
  put(ur_cons(r, cord("connect"),
        ur_cons(r, wire(w), ur_cons(r, target, timeout))));
}

static void
put_connect_if(const char *w, uint32_t ip, uint16_t port)
{
  put_connect(w, fief("if", ur_coin64(r, ip), ur_coin64(r, port)), 0);
}

// [%send wire [len data]]
static void
put_octs(const char *w, ur_nref len, ur_nref data)
{
  put(ur_cons(r, cord("send"), ur_cons(r, wire(w), ur_cons(r, len, data))));
}

static void
put_send(const char *w, const uint8_t *data, size_t len)
{
  put_octs(w, ur_coin64(r, len), ur_coin_bytes(r, len, (uint8_t*)data));
}

static void
put_close(const char *w)
{
  put(ur_cons(r, cord("close"), wire(w)));
}

// --- Gifts ---

typedef struct {
  char     tag[16];
  char     wire[128];
  char     msg[256];   // %error
  uint8_t *data;       // %receive
  size_t   len;
} gift_t;

// Copy an atom's bytes into buf, zero-padded to len
static void
atom_bytes(ur_nref ref, uint8_t *buf, size_t len)
{
  memset(buf, 0, len);
  if ( ur_nref_tag(ref) == ur_direct ) {
    uint64_t val = ur_nref_idx(ref);
    for ( size_t i = 0; i < len && i < 8; i++ ) {
      buf[i] = (val >> (8 * i)) & 0xff;
    }
  }
  else if ( ur_nref_tag(ref) == ur_iatom ) {
    uint64_t idx = ur_nref_idx(ref);
    uint64_t has = r->atoms.lens[idx];
    memcpy(buf, r->atoms.bytes[idx], has < len ? has : len);
  }
}

static int
cell(ur_nref ref, ur_nref *head, ur_nref *tail)
{
  if ( ur_nref_tag(ref) != ur_icell ) return -1;
  *head = r->cells.heads[ur_nref_idx(ref)];
  *tail = r->cells.tails[ur_nref_idx(ref)];
  return 0;
}

// Read the next gift. Returns 0, or -1 if none arrives within ms.
// The caller frees g->data.
static int
get(gift_t *g, int ms)
{
  memset(g, 0, sizeof(*g));

  uint8_t hdr[5];
  if ( read_n(lick, hdr, 5, ms) < 5 ) return -1;

  uint32_t jam_len = ((uint32_t)hdr[4] << 24) | ((uint32_t)hdr[3] << 16)
                   | ((uint32_t)hdr[2] << 8)  |  (uint32_t)hdr[1];
  uint8_t *jam_byt = malloc(jam_len);
  if ( read_n(lick, jam_byt, jam_len, WAIT_MS) < jam_len ) {
    free(jam_byt);
    return -1;
  }

  ur_nref noun, mark, body, tag, rest, w, extra = 0;
  int bad = ur_cue(r, jam_len, jam_byt, &noun) != ur_cue_good
         || cell(noun, &mark, &body) < 0
         || cell(body, &tag, &rest) < 0;
  free(jam_byt);
  if ( bad ) return -1;

  atom_bytes(tag, (uint8_t*)g->tag, sizeof(g->tag) - 1);

  // %connected and %closed carry the wire alone
  if (  strcmp(g->tag, "connected") == 0
     || strcmp(g->tag, "closed") == 0 ) {
    w = rest;
  }
  else if ( cell(rest, &w, &extra) < 0 ) {
    return -1;
  }

  size_t pos = 0;
  ur_nref seg;
  while ( cell(w, &seg, &w) == 0 && pos + 10 < sizeof(g->wire) ) {
    g->wire[pos++] = '/';
    atom_bytes(seg, (uint8_t*)g->wire + pos, 8);
    pos += strlen(g->wire + pos);
  }

  if ( strcmp(g->tag, "error") == 0 ) {
    atom_bytes(extra, (uint8_t*)g->msg, sizeof(g->msg) - 1);
  }
  else if ( strcmp(g->tag, "receive") == 0 ) {
    ur_nref len, data;
    if ( cell(extra, &len, &data) < 0 ) return -1;
    g->len = ur_nref_idx(len);
    g->data = malloc(g->len ? g->len : 1);
    atom_bytes(data, g->data, g->len);
  }

  ur_root_free(r);
  r = ur_root_init();
  return 0;
}

// Check that the next gift is [tag /wire], with msg if it is an %error
static void
expect(const char *tag, const char *w, const char *msg)
{
  gift_t g;
  char what[512], path[130];
  snprintf(path, sizeof(path), "/%s", w);
  snprintf(what, sizeof(what), "%%%s %s%s%s", tag, path,
           msg ? ": " : "", msg ? msg : "");

  int ok = get(&g, WAIT_MS) == 0
        && strcmp(g.tag, tag) == 0
        && strcmp(g.wire, path) == 0
        && (!msg || strcmp(g.msg, msg) == 0);
  check(ok, what);
  if ( !ok ) {
    printf("        got %%%s %s %s\n", g.tag[0] ? g.tag : "(nothing)",
           g.wire, g.msg);
  }
  free(g.data);
}

// --- The TCP side ---

// Listen on a loopback port the kernel picks. Returns -1 if the family
// is not available.
static int
tcp_listen(int family, uint16_t *port)
{
  int fd = socket(family, SOCK_STREAM, 0);
  if ( fd < 0 ) return -1;

  struct sockaddr_storage ss = {0};
  socklen_t len;
  if ( family == AF_INET6 ) {
    struct sockaddr_in6 *a = (struct sockaddr_in6*)&ss;
    a->sin6_family = AF_INET6;
    a->sin6_addr = in6addr_loopback;
    len = sizeof(*a);
  } else {
    struct sockaddr_in *a = (struct sockaddr_in*)&ss;
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    len = sizeof(*a);
  }

  if (  bind(fd, (struct sockaddr*)&ss, len) < 0
     || listen(fd, 8) < 0
     || getsockname(fd, (struct sockaddr*)&ss, &len) < 0 ) {
    close(fd);
    return -1;
  }
  *port = ntohs(family == AF_INET6
                ? ((struct sockaddr_in6*)&ss)->sin6_port
                : ((struct sockaddr_in*)&ss)->sin_port);
  return fd;
}

static int
tcp_accept(int srv, int ms)
{
  struct pollfd p = { .fd = srv, .events = POLLIN };
  if ( poll(&p, 1, ms) <= 0 ) return -1;
  return accept(srv, NULL, NULL);
}

// Open a plain connection on /w through the sidecar; returns the peer fd
static int
open_conn(const char *w, int srv, uint16_t port)
{
  put_connect_if(w, 0x7f000001, port);
  expect("connected", w, NULL);
  int fd = tcp_accept(srv, WAIT_MS);
  if ( fd < 0 ) die("accept");
  return fd;
}

// --- Tests ---

// Connect, send, receive, close
static void
test_basic(void)
{
  printf("basic\n");
  uint16_t port;
  int srv = tcp_listen(AF_INET, &port);
  if ( srv < 0 ) die("tcp_listen");

  int fd = open_conn("basic", srv, port);

  uint8_t buf[16];
  put_send("basic", (uint8_t*)"hello", 5);
  check(read_n(fd, buf, 5, WAIT_MS) == 5 && memcmp(buf, "hello", 5) == 0,
        "peer reads what %send carried");

  // octs longer than the atom: the trailing zeros are part of the data
  put_octs("basic", ur_coin64(r, 5), cord("hi"));
  check(read_n(fd, buf, 5, WAIT_MS) == 5 && memcmp(buf, "hi\0\0\0", 5) == 0,
        "octs keep their trailing zeros");

  write_all(fd, (uint8_t*)"world", 5);
  gift_t g;
  check(get(&g, WAIT_MS) == 0 && strcmp(g.tag, "receive") == 0
        && g.len == 5 && memcmp(g.data, "world", 5) == 0,
        "%receive carries what the peer wrote");
  free(g.data);

  put_close("basic");
  expect("closed", "basic", NULL);
  check(saw_eof(fd, WAIT_MS), "peer sees the close");

  close(fd);
  close(srv);
}

// Issue 6: an atom too wide for its field gets an %error, not a zero
static void
test_wide_atoms(void)
{
  printf("wide atoms\n");
  uint16_t port;
  int srv = tcp_listen(AF_INET, &port);
  if ( srv < 0 ) die("tcp_listen");
  ur_nref good_ip, good_port;

#define GOOD() \
  good_ip = ur_coin64(r, 0x7f000001); good_port = ur_coin64(r, port)

  // An IPv6 address under %if: once dialled as 0.0.0.0, which reaches
  // loopback, so the listener would see it
  GOOD();
  put_connect("a", fief("if", wide(16), good_port), 0);
  expect("error", "a", "bad address");
  check(tcp_accept(srv, 300) < 0, "nothing was dialled");

  GOOD();
  put_connect("a", fief("if", ur_coin64(r, 0x100000000ULL), good_port), 0);
  expect("error", "a", "bad address");

  GOOD();
  put_connect("a", fief("is", wide(17), good_port), 0);
  expect("error", "a", "bad address");

  GOOD();
  put_connect("a", fief("if", good_ip, wide(9)), 0);
  expect("error", "a", "bad port");

  GOOD();
  put_connect("a", fief("if", good_ip, ur_coin64(r, 65536)), 0);
  expect("error", "a", "bad port");

  GOOD();
  put_connect("a", fief("if", good_ip, 0), 0);
  expect("error", "a", "bad port");

  GOOD();
  put_connect("a", fief("if", good_ip, good_port), unit(wide(9)));
  expect("error", "a", "bad timeout");

  // 8 bytes: an indirect atom that still fits in 64 bits
  GOOD();
  put_connect("a", fief("if", good_ip, good_port), unit(wide(8)));
  expect("error", "a", "bad timeout");

  // A timeout that fits still connects
  GOOD();
  put_connect("a", fief("if", good_ip, good_port), unit(ur_coin64(r, 2000)));
  expect("connected", "a", NULL);
  int fd = tcp_accept(srv, WAIT_MS);
  check(fd >= 0, "a good target still connects");

  // A bad length on %send ends the connection: the agent drops the wire
  // on %error, so the sidecar must not keep the socket
  put_octs("a", wide(9), cord("x"));
  expect("error", "a", "bad octs");
  check(saw_eof(fd, WAIT_MS), "bad octs closes the connection");
  close(fd);

  // IPv6 loopback, the address as a 16-byte atom
  uint16_t port6;
  int srv6 = tcp_listen(AF_INET6, &port6);
  if ( srv6 < 0 ) {
    printf("  skip  no IPv6 loopback\n");
  } else {
    put_connect("six", fief("is", ur_coin64(r, 1), ur_coin64(r, port6)), 0);
    expect("connected", "six", NULL);
    put_close("six");
    expect("closed", "six", NULL);
    close(srv6);
  }

#undef GOOD
  close(srv);
}

int
main(void)
{
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, on_alarm);
  alarm(120);
  setbuf(stdout, NULL);

  if ( !realpath("./tcp-sidecar", sidecar_bin) ) die("./tcp-sidecar");

  // Work inside a fresh pier, so every socket path is short and relative
  const char *tmp = getenv("TMPDIR");
  snprintf(pier, sizeof(pier), "%s/tcp-sidecar-test.XXXXXX",
           tmp && *tmp ? tmp : "/tmp");
  if ( !mkdtemp(pier) ) die("mkdtemp");
  if ( chdir(pier) < 0 ) die("chdir");

  r = ur_root_init();

  lick_listen();
  sidecar_start();
  if ( lick_accept(WAIT_MS) < 0 ) die("the sidecar did not connect");

  test_basic();
  test_wide_atoms();

  check(sidecar_alive(), "the sidecar is still running");

  printf("%d checks, %d failed\n", checks, fails);
  if ( fails ) printf("sidecar log: %s/%s\n", pier, LOG_PATH);
  cleanup();
  return fails ? 1 : 0;
}
