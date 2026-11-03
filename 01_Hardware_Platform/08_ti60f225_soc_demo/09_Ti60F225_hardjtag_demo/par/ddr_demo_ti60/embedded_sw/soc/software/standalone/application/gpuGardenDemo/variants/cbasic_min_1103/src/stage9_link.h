/* GPL-3.0-or-later. RTL14 persistent-repeat handoff; candidate until board checked. */
#pragma once
#include <stdint.h>
/* Caller finishes GPU writes to physical back FB and publishes independent DDR
 * atlas/descriptor staging before present. Neither current stage9 input nor
 * its background may be changed while armed/busy. Atlas fixed 16KB; entries16B.
 * All mature enemy pixels use global opaque/key mode; other effects stay legacy.
 * present requests the legacy FB swap itself; caller then calls render_end as
 * usual (same-buffer request is harmless). It never clears hardware counters.
 * Subsequent handoffs align to a NEW successful480-row display completion;
 * cancellation/config/rearm must fit3ms, before legacy swap719/new tick720.
 * Failure cancels/disables and latches error; caller must stop and preserve
 * evidence. Disable alone cannot reconstruct omitted bullets in a legacy FB.
 */
typedef struct {
    uint32_t available,running,submissions,errors;
    int32_t last_error;
    union { uint32_t displayed_frames,displayed; };
    union { uint32_t engine_frames,engine; };
    union { uint32_t missing_pixels,missing_px; };
    uint32_t missing_lines;
    union { uint32_t skipped_frames,skipped; };
    union { uint32_t rejected_writes,rejected; };
    union { uint32_t max_line_cycles,max_line; };
    uint32_t last_frame_cycles,status;
    uint32_t initial_displayed_frames,initial_engine_frames,initial_missing_pixels;
    uint32_t initial_missing_lines,initial_rejected_writes,initial_skipped_frames;
    uint64_t wait_ticks,config_ticks,max_config_ticks,fallback_ticks;
} Stage9LinkStats;
enum {STAGE9_LINK_OK=0,STAGE9_LINK_EID=-1,STAGE9_LINK_EINVAL=-2,
      STAGE9_LINK_EDISPLAY=-3,STAGE9_LINK_EDRAIN=-4,STAGE9_LINK_EDEADLINE=-5,
      STAGE9_LINK_EATLAS=-6,STAGE9_LINK_EREJECT=-7,STAGE9_LINK_EFRAME=-8,
      STAGE9_LINK_EFB=-9,STAGE9_LINK_ESTATE=-10};
int stage9_link_init(void);
/* Set only while the C handoff owns the page; applied during next quiescent config. */
void stage9_link_alpha(unsigned alpha);
/* Applied with alpha only after the active scan is cancelled and drained. */
void stage9_link_key(uint16_t key);
int stage9_link_present(uint32_t atlas,uint32_t desc,unsigned count,uint32_t bg,unsigned back_sel);
int stage9_link_stop(void);
void stage9_link_snapshot(Stage9LinkStats *out);
