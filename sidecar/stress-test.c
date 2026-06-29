// Stress test for ur_root free/init cycling
// Simulates what the sidecar does thousands of times per session:
// init root -> build nouns -> jam -> free root -> repeat
//
// Run with ASAN: make clean && make CFLAGS="-O0 -g -fsanitize=address -I." stress-test

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ur/ur.h"

// Simulate send_receive: build [mark [tag [wire [len data]]]] and jam it
static void
simulate_gift(ur_root_t *r)
{
  // Build a wire: [%foo %bar ~]
  ur_nref seg1 = ur_coin_bytes(r, 3, (uint8_t*)"foo");
  ur_nref seg2 = ur_coin_bytes(r, 3, (uint8_t*)"bar");
  ur_nref wire = ur_cons(r, seg1, ur_cons(r, seg2, 0));

  // Build octs: [p=@ud q=@]
  ur_nref n_len = ur_coin64(r, 1);
  ur_nref n_data = ur_coin_bytes(r, 1, (uint8_t*)"x");
  ur_nref octs = ur_cons(r, n_len, n_data);

  // Build payload: [%receive wire octs]
  ur_nref tag = ur_coin_bytes(r, 7, (uint8_t*)"receive");
  ur_nref inner = ur_cons(r, wire, octs);
  ur_nref payload = ur_cons(r, tag, inner);

  // Build message: [%tcp-gift payload]
  ur_nref mark = ur_coin_bytes(r, 8, (uint8_t*)"tcp-gift");
  ur_nref msg = ur_cons(r, mark, payload);

  // Jam it (like make_lick_msg does)
  uint64_t jam_len;
  uint8_t *jam_byt;
  ur_jam(r, msg, &jam_len, &jam_byt);
  free(jam_byt);
}

// Simulate ur_cue of incoming task data
static void
simulate_task(ur_root_t *r)
{
  // Build a task noun, jam it, then cue it back
  ur_nref mark = ur_coin_bytes(r, 8, (uint8_t*)"tcp-task");
  ur_nref cmd = ur_coin_bytes(r, 4, (uint8_t*)"send");
  ur_nref seg = ur_coin_bytes(r, 4, (uint8_t*)"conn");
  ur_nref wire = ur_cons(r, seg, 0);
  ur_nref n_len = ur_coin64(r, 5);
  ur_nref n_data = ur_coin_bytes(r, 5, (uint8_t*)"hello");
  ur_nref octs = ur_cons(r, n_len, n_data);
  ur_nref payload = ur_cons(r, cmd, ur_cons(r, wire, octs));
  ur_nref msg = ur_cons(r, mark, payload);

  uint64_t jam_len;
  uint8_t *jam_byt;
  ur_jam(r, msg, &jam_len, &jam_byt);

  // Now cue it back (simulates receiving from lick)
  ur_nref out;
  ur_cue_res_e res = ur_cue(r, jam_len, jam_byt, &out);
  if ( res != ur_cue_good ) {
    printf("FAIL: cue failed at some point\n");
  }
  free(jam_byt);
}

int
main(void)
{
  // 5 minutes at 100ms poll = ~3000 cycles minimum
  // Plus extra for each message received
  int cycles = 10000;

  printf("stress-test: %d cycles of ur_root init/free with noun ops\n", cycles);

  for ( int i = 0; i < cycles; i++ ) {
    ur_root_t *r = ur_root_init();

    // Alternate between gift and task simulation
    if ( i % 2 == 0 ) {
      simulate_gift(r);
    } else {
      simulate_task(r);
    }

    ur_root_free(r);

    if ( (i + 1) % 1000 == 0 ) {
      printf("  completed %d/%d cycles\n", i + 1, cycles);
    }
  }

  printf("stress-test: PASSED (no crash in %d cycles)\n", cycles);
  return 0;
}
